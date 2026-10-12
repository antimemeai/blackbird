#include "blackbird/colleague.hpp"
#include "blackbird/json.hpp"
#include "blackbird/tools.hpp"
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace blackbird;
int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: blackbird-colleague REQUEST.json NEW_CAPTURE_DIRECTORY\n";
    return 2;
  }
  try {
    // Native exclusive setup happens before source acquisition or dispatch.
    if (::mkdir(argv[2], 0700) != 0)
      throw Error{ErrorCode::io, errno};
    const auto directory = std::filesystem::canonical(argv[2]);
    struct stat owned{};
    if (::lstat(directory.c_str(), &owned) != 0)
      throw Error{ErrorCode::io, errno};
    if (!S_ISDIR(owned.st_mode) || owned.st_uid != ::geteuid() ||
        (owned.st_mode & 0777) != 0700)
      throw Error{ErrorCode::conflict};
    IdentityBytes identity{};
    if (::getentropy(identity.data(), identity.size()) != 0)
      throw Error{ErrorCode::io, errno};
    auto runtime = unwrap(RawRuntime::open(
        directory.string(), unwrap(EnvironmentId::from_bytes(identity))));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    RawProviderPhase input_phase;
    const auto request = input_phase.run([&] {
      input_phase.start(11);
      // Header must use the actual live phase, so stage source after its BEGIN.
      ProviderOriginalLease original{{ProviderOriginalRole::standalone_request,
                                      ProviderOriginalMode::codec_adopt,
                                      65536,
                                      {}},
                                     input_phase.reference()};
      original.adopt(read_file(argv[1], 65536));
      return unwrap(parse_json(original.bytes()));
    });

    std::ofstream raw;
    raw.exceptions(std::ios::failbit | std::ios::badbit);
    raw.open(directory / "raw.bin", std::ios::binary);
    const auto capture = [&](std::string_view kind, std::string_view bytes) {
      if (kind == "raw") {
        raw.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        raw.flush();
      } else {
        write_file(directory / (std::string{kind} + ".bbm"), bytes);
      }
    };
    const auto result = call_colleague(
        request, capture,
        [](const Value &prepared, const ColleagueCapture &retain,
           const RawAdapterContext *context) {
          return native_colleague_transport(prepared, retain, {}, nullptr, context);
        });
    std::cout << unwrap(dump_json(result)) << '\n';
    return result.find("status")->string() == "completed" ? 0 : 1;
  } catch (const Error &e) {
    std::cerr << "colleague stopped: " << error_name(e.code)
              << "; inspect retained capture, do not replay\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "colleague stopped: " << e.what()
              << "; inspect retained capture, do not replay\n";
    return 1;
  }
}
