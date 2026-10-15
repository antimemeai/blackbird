#include "blackbird/json.hpp"
#include "blackbird/tools.hpp"
#include <iostream>
#include <unistd.h>
using namespace blackbird;
int main() {
  char name[] = "/tmp/arco-tools-XXXXXX";
  auto path = mkdtemp(name);
  if (!path)
    return 2;
  try {
    LocalTools tools;
    const auto file = std::string{path} + "/source.cpp";
    tools.run("write_file", Value::object({{"path", Value{file}},
                                           {"content", Value{"int answer = 1;\n"}}}));
    tools.run("edit_file", Value::object({{"path", Value{file}},
                                          {"old", Value{"= 1"}},
                                          {"new", Value{"= 42"}}}));
    if (read_file(file) != "int answer = 42;\n")
      throw Error{ErrorCode::corrupt};
    auto result = tools.run(
        "exec",
        Value::object({{"command", Value{"printf OUT; printf ERR >&2; exit 7"}}}));
    if (string_field(result, "output") != "OUTERR" ||
        field(result, "exit_code").number().text() != "7")
      throw Error{ErrorCode::corrupt};
    if (tools.captured().size() != 1 || tools.captured()[0].bytes != "OUTERR")
      throw Error{ErrorCode::corrupt};
    auto fallback =
        tools.run("exec", Value::object({{"command", Value{"printf EMPTY_ARGV_OK"}},
                                         {"argv", Value{Value::Array{}}}}));
    if (string_field(fallback, "output") != "EMPTY_ARGV_OK")
      throw Error{ErrorCode::corrupt};
    auto bounded =
        tools.run("exec", Value::object({{"command", Value{"printf abcdef; exit 9"}},
                                         {"output_max_bytes", Value{Number{"2"}}}}));
    if (string_field(bounded, "output") != "ab" ||
        field(bounded, "omitted_bytes").number().text() != "4" ||
        field(bounded, "exit_code").number().text() != "9" ||
        tools.captured()[0].bytes != "abcdef")
      throw Error{ErrorCode::corrupt};
    auto zero =
        tools.run("exec", Value::object({{"command", Value{"printf x"}},
                                         {"output_max_bytes", Value{Number{"0"}}}}));
    if (string_field(zero, "output") != "" ||
        field(zero, "output_bytes").number().text() != "1")
      throw Error{ErrorCode::corrupt};
    auto split =
        tools.run("exec", Value::object({{"command", Value{"printf '\\303\\251'"}},
                                         {"output_max_bytes", Value{Number{"1"}}}}));
    if (string_field(field(split, "output"), "bytes") != "c3")
      throw Error{ErrorCode::corrupt};
    const auto marker = std::string{path} + "/nul-command-marker";
    for (const auto *number : {"-1", "1.5", "18446744073709551616"}) {
      bool rejected = false;
      try {
        tools.run("exec", Value::object({{"command", Value{"touch '" + marker + "'"}},
                                         {"output_max_bytes", Value{Number{number}}}}));
      } catch (const Error &e) {
        rejected = e.code == ErrorCode::invalid_range;
      }
      if (!rejected || std::filesystem::exists(marker))
        throw Error{ErrorCode::corrupt};
    }
    auto command = "touch '" + marker + "'";
    command.push_back('\0');
    command += "ignored suffix";
    bool command_refused = false;
    try {
      tools.run("exec", Value::object({{"command", Value{command}}}));
    } catch (const Error &e) {
      command_refused = e.code == ErrorCode::invalid_range;
    }
    const bool marker_created = std::filesystem::exists(marker);
    if (!command_refused || marker_created) {
      std::cerr << "embedded-NUL command: refused=" << command_refused
                << ", marker_created=" << marker_created << '\n';
      throw Error{ErrorCode::corrupt};
    }
    write_file(file, "a\r\n\nb");
    const auto projected = tools.run(
        "read_file",
        Value::object({{"path", Value{file}},
                       {"range", Value::object({{"mode", Value{"bytes"}},
                                                {"start", Value{Number{1}}}})},
                       {"output_max_bytes", Value{Number{2}}}}));
    if (string_field(projected, "content") != "\r\n" ||
        field(projected, "file_bytes") != Value{Number{5}} ||
        field(projected, "selected_bytes") != Value{Number{4}} ||
        field(projected, "returned_bytes") != Value{Number{2}} ||
        field(projected, "omitted_bytes") != Value{Number{2}} ||
        tools.captured()[0].bytes != "a\r\n\nb")
      throw Error{ErrorCode::corrupt};
    for (const auto *bad : {"-1", "1.5", "18446744073709551616"}) {
      bool rejected = false;
      try {
        tools.run("read_file",
                  Value::object({{"path", Value{file}},
                                 {"output_max_bytes", Value{Number{bad}}}}));
      } catch (const Error &e) {
        rejected = e.code == ErrorCode::invalid_range;
      }
      if (!rejected)
        throw Error{ErrorCode::corrupt};
    }
    const auto empty_view = tools.run(
        "read_file",
        Value::object({{"path", Value{file}}, {"output_max_bytes", Value{Number{0}}}}));
    if (string_field(empty_view, "content") != "" ||
        field(empty_view, "omitted_bytes") != Value{Number{5}})
      throw Error{ErrorCode::corrupt};
    for (const auto &selector :
         {Value::object({{"range", Value::object({{"mode", Value{"lines"}},
                                                  {"start", Value{Number{1}}},
                                                  {"end", Value{Number{2}}}})}}),
          Value::object(
              {{"line_start", Value{Number{1}}}, {"line_end", Value{Number{2}}}})}) {
      auto args = Value{selector};
      args.object().emplace_back("path", Value{file});
      args.object().emplace_back("output_max_bytes", Value{Number{2}});
      const auto limited = tools.run("read_file", args);
      if (string_field(limited, "content") != "a\r" ||
          field(limited, "selected_bytes") != Value{Number{4}} ||
          field(limited, "omitted_bytes") != Value{Number{2}})
        throw Error{ErrorCode::corrupt};
    }
    write_file(file, std::string{"x\xc3\xa9\xff", 4});
    const auto binary_prefix = tools.run(
        "read_file",
        Value::object({{"path", Value{file}}, {"output_max_bytes", Value{Number{2}}}}));
    if (string_field(field(binary_prefix, "content"), "bytes") != "78c3" ||
        field(binary_prefix, "returned_bytes") != Value{Number{2}} ||
        field(binary_prefix, "omitted_bytes") != Value{Number{2}} ||
        tools.captured()[0].bytes != std::string{"x\xc3\xa9\xff", 4})
      throw Error{ErrorCode::corrupt};
    write_file(file, "a\r\n\nb");
    auto range = [&](const std::string &fields, const std::string &expected) {
      auto args = unwrap(parse_json("{\"path\":\"" + file + "\"," + fields + "}"));
      auto got = tools.run("read_file", args);
      if (string_field(got, "content") != expected || tools.captured().size() != 1 ||
          tools.captured()[0].bytes != read_file(file))
        throw Error{ErrorCode::corrupt};
    };
    range(R"("range":{"mode":"lines","start":2,"end":2})", "\n");
    range(R"("range":{"mode":"lines","start":3})", "b");
    range(R"("range":{"mode":"bytes","start":1,"end":3})", "\r\n");
    range(R"("range":{"mode":"bytes","start":99})", "");
    range(R"("range":{"mode":"lines"})", "a\r\n\nb");
    for (const auto *bad :
         {R"("range":{"mode":"lines","bytes":{}})", R"("range":{"mode":"both"})",
          R"("range":{"mode":"lines","start":0})",
          R"("range":{"mode":"bytes","start":-1})",
          R"("range":{"mode":"bytes","start":3,"end":2})",
          R"("range":{"mode":"bytes","start":null})", R"("range":{})", R"("range":[])",
          R"("range":null)", R"("range":{"mode":"lines"},"byte_start":0)",
          R"("range":{"mode":"bytes"},"line_end":2)"}) {
      bool rejected = false;
      try {
        range(bad, "");
      } catch (const Error &e) {
        rejected = e.code == ErrorCode::invalid_range;
      }
      if (!rejected)
        throw Error{ErrorCode::corrupt};
    }
    bool canonical_schema = false;
    const auto definitions = tool_definitions();
    for (const auto &definition : definitions.array()) {
      if (string_field(definition, "name") != "read_file")
        continue;
      const auto &schema = field(definition, "parameters");
      const auto &properties = field(schema, "properties");
      canonical_schema = properties.find("range") && !properties.find("byte_start") &&
                         !properties.find("line_start") &&
                         !std::get<bool>(field(schema, "additionalProperties").value());
    }
    if (!canonical_schema)
      throw Error{ErrorCode::corrupt};
    range("\"line_start\":2,\"line_end\":2", "\n");
    range("\"line_start\":3", "b");
    range("\"line_start\":4", "");
    range("\"line_end\":99", "a\r\n\nb");
    range("\"byte_start\":1,\"byte_end\":3", "\r\n");
    range("\"byte_start\":99", "");
    range("\"byte_start\":2,\"byte_end\":2", "");
    for (const auto *bad :
         {"\"line_start\":0", "\"byte_start\":-1", "\"byte_end\":1.5",
          "\"line_end\":true", "\"byte_start\":3,\"byte_end\":2",
          "\"line_start\":3,\"line_end\":2", "\"line_start\":1,\"byte_end\":2",
          "\"byte_start\":18446744073709551616"}) {
      bool rejected = false;
      try {
        range(bad, "");
      } catch (const Error &e) {
        rejected = e.code == ErrorCode::invalid_range;
      }
      if (!rejected)
        throw Error{ErrorCode::corrupt};
    }
    write_file(file, "");
    range("\"line_start\":1", "");
    write_file(file, "x\n");
    range("\"line_start\":2", "");
    write_file(file, std::string{"x\0\xff", 3});
    range("\"byte_start\":1,\"byte_end\":2", std::string(1, '\0'));
    auto binary = tools.run(
        "read_file",
        Value::object({{"path", Value{file}}, {"byte_start", Value{Number{"2"}}}}));
    if (string_field(field(binary, "content"), "bytes") != "ff" ||
        tools.captured()[0].bytes != std::string{"x\0\xff", 3})
      throw Error{ErrorCode::corrupt};
    write_file(file, "same same");
    bool refused = false;
    try {
      tools.run("edit_file", Value::object({{"path", Value{file}},
                                            {"old", Value{"same"}},
                                            {"new", Value{"bad"}}}));
    } catch (const Error &e) {
      refused = e.code == ErrorCode::conflict;
    }
    if (!refused || read_file(file) != "same same")
      throw Error{ErrorCode::corrupt};
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
  std::filesystem::remove_all(path);
}
