#include "blackbird/context.hpp"
#include "blackbird/journal.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include "native_process.hpp"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#ifndef __APPLE__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace {
using namespace blackbird;
namespace fs = std::filesystem;
constexpr std::size_t maximum_packet = 16 * 1024 * 1024;
struct File {
  int fd = -1;
  File(const fs::path &path, int flags, mode_t mode = 0600)
      : fd(::open(path.c_str(), flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, mode)) {
    if (fd < 0)
      throw Error{ErrorCode::io, errno};
    struct stat status{};
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode)) {
      (void)::close(fd);
      throw Error{ErrorCode::invalid_range};
    }
  }
  ~File() { (void)::close(fd); }
  File(const File &) = delete;
  File &operator=(const File &) = delete;
};
[[noreturn]] void fail(ErrorCode code, std::int64_t detail = 0) {
  throw Error{code, detail};
}
void directory(const fs::path &path) {
  const bool created = fs::create_directories(path);
  if (!fs::is_directory(fs::symlink_status(path)))
    fail(ErrorCode::invalid_range);
  if (created)
    fs::permissions(path, fs::perms::owner_all);
}
std::uint64_t integer(std::string_view text) {
  std::uint64_t number = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    fail(ErrorCode::invalid_range);
  return number;
}
std::uint64_t number(const Value &value, std::string_view key) {
  const auto &n = field(value, key).number().storage();
  if (const auto *v = std::get_if<std::uint64_t>(&n))
    return *v;
  if (const auto *v = std::get_if<std::int64_t>(&n); v && *v >= 0)
    return static_cast<std::uint64_t>(*v);
  fail(ErrorCode::invalid_range);
}
Value num(std::uint64_t n) { return Value{Number{n}}; }
void write_all(int descriptor, ByteView bytes) {
  while (!bytes.empty()) {
    const auto n = ::write(descriptor, bytes.data(), bytes.size());
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      fail(ErrorCode::io, errno);
    bytes = bytes.subspan(static_cast<std::size_t>(n));
  }
}
std::vector<std::byte> read_all(const fs::path &path, std::size_t maximum) {
  File file{path, O_RDONLY};
  struct stat status{};
  if (::fstat(file.fd, &status) != 0 || status.st_size < 0 ||
      static_cast<std::uint64_t>(status.st_size) > maximum)
    fail(ErrorCode::capacity);
  std::vector<std::byte> bytes(static_cast<std::size_t>(status.st_size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = ::read(file.fd, bytes.data() + offset, bytes.size() - offset);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      fail(ErrorCode::incomplete);
    offset += static_cast<std::size_t>(n);
  }
  return bytes;
}
void sync_directory(const fs::path &path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    fail(ErrorCode::io, errno);
  const int result = ::fsync(fd), error = errno;
  (void)::close(fd);
  if (result != 0)
    fail(ErrorCode::io, error);
}
void atomic_file(const fs::path &path, ByteView bytes) {
  const auto temporary = path.string() + ".tmp";
  {
    File file{temporary, O_WRONLY | O_CREAT | O_TRUNC};
    write_all(file.fd, bytes);
    if (::fsync(file.fd) != 0)
      fail(ErrorCode::io, errno);
  }
  fs::rename(temporary, path);
  sync_directory(path.parent_path());
}
void packet_file(const fs::path &path, const Value &value) {
  const auto bytes = unwrap(encode_packet(value));
  atomic_file(path, bytes);
}
Value packet(const fs::path &path) {
  return unwrap(decode_packet(read_all(path, maximum_packet)));
}
struct Program {
  const char *environment;
  const char *fallback;
};
std::string executable(Program program) {
  const char *value = ::getenv(program.environment);
  return value && *value ? std::string{value} : std::string{program.fallback};
}
std::string process(std::vector<std::string> args, std::string_view input = {}) {
  detail::Child child;
  child.start(std::move(args));
  const auto end = detail::Clock::now() + std::chrono::seconds{30};
  child.send(input, end);
  child.close_input();
  return child.collect(end, 65536);
}
std::string hex(std::string_view bytes) {
  constexpr char alphabet[] = "0123456789abcdef";
  std::string result;
  for (char raw : bytes) {
    const auto byte = static_cast<unsigned char>(raw);
    result += alphabet[byte >> 4U];
    result += alphabet[byte & 15U];
  }
  return result;
}
std::string hash(const fs::path &path) {
  if (!fs::is_regular_file(fs::symlink_status(path)))
    fail(ErrorCode::invalid_range);
  const auto digest = process({"openssl", "dgst", "-sha256", "-binary", path.string()});
  if (digest.size() != 32)
    fail(ErrorCode::corrupt);
  return hex(digest);
}
bool digest_name(std::string_view name, std::string_view suffix) {
  return name.size() == 64 + suffix.size() && name.ends_with(suffix) &&
         std::all_of(name.begin(), name.begin() + 64, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
std::string checksum_base64(std::string_view digest) {
  constexpr std::string_view hex_digits = "0123456789abcdef";
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  unsigned bits = 0, count = 0;
  for (std::size_t i = 0; i < 64; i += 2) {
    bits = (bits << 8U) | static_cast<unsigned>((hex_digits.find(digest[i]) << 4U) |
                                                hex_digits.find(digest[i + 1]));
    count += 8;
    while (count >= 6) {
      count -= 6;
      output += alphabet[(bits >> count) & 63U];
    }
  }
  if (count)
    output += alphabet[(bits << (6U - count)) & 63U];
  while (output.size() % 4 != 0)
    output += '=';
  return output;
}
std::string quote(std::string_view text) {
  std::string result = "\"";
  for (char c : text) {
    if (static_cast<unsigned char>(c) < 32 || c == 127)
      fail(ErrorCode::invalid_range);
    if (c == '\\' || c == '"')
      result += '\\';
    result += c;
  }
  return result + '"';
}
std::string shell_literal(std::string_view text) {
  std::string result = "'";
  for (char c : text)
    result += c == '\'' ? "'\\''" : std::string(1, c);
  return result + '\'';
}
struct Config {
  std::string target, region;
  std::uint64_t chunk = 16 * 1024 * 1024, sweep = 16 * 1024 * 1024, interval = 60;
  enum class Kind { local, scp, s3 } kind = Kind::local;
  void validate() {
    if (target.empty() || target.size() > 4096 || chunk < 4096 ||
        chunk > 64 * 1024 * 1024 || sweep == 0 || interval == 0 || interval > 86400)
      fail(ErrorCode::invalid_range);
    (void)quote(target);
    if (target.starts_with("s3://")) {
      kind = Kind::s3;
      const auto body = std::string_view{target}.substr(5);
      const auto slash = body.find('/');
      const auto bucket = body.substr(0, slash);
      if (bucket.empty() || region.empty() ||
          !std::all_of(bucket.begin(), bucket.end(),
                       [](char c) {
                         return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '.';
                       }) ||
          !std::all_of(region.begin(), region.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
          }))
        fail(ErrorCode::invalid_range);
    } else if (target.front() == '/') {
      kind = Kind::local;
    } else {
      kind = Kind::scp;
      if (target.starts_with("scp://")) {
        const auto slash = target.find('/', 6);
        if (slash == std::string::npos || slash == 6)
          fail(ErrorCode::invalid_range);
      } else {
        const auto colon = target.find(':');
        if (colon == 0 || colon == std::string::npos || colon + 1 >= target.size() ||
            target[colon + 1] != '/' || target.front() == '-')
          fail(ErrorCode::invalid_range);
      }
      // Keep SFTP's wildcard expansion out of object names and base paths.
      if (target.find_first_of("*?[]{}\\") != std::string::npos)
        fail(ErrorCode::invalid_range);
    }
    while (target.size() > 1 && target.back() == '/' && !target.ends_with(":/") &&
           !(target.starts_with("scp://") && target.find('/', 6) == target.size() - 1))
      target.pop_back();
  }
  Value value() const {
    return Value::object({{"version", num(1)},
                          {"target", Value{target}},
                          {"region", Value{region}},
                          {"chunk", num(chunk)},
                          {"sweep", num(sweep)},
                          {"interval", num(interval)}});
  }
};
void validate_target(const Config &config, const fs::path &session) {
  if (config.kind != Config::Kind::local)
    return;
  const auto root = fs::weakly_canonical(session);
  const auto target = fs::weakly_canonical(config.target);
  const auto relative = target.lexically_relative(root);
  if (!relative.empty() && *relative.begin() != "..")
    fail(ErrorCode::invalid_range);
}
Config configuration(const fs::path &session) {
  const auto value = packet(session / "trace-archive.bbm");
  if (number(value, "version") != 1)
    fail(ErrorCode::unsupported);
  Config config;
  config.target = field(value, "target").string();
  config.region = field(value, "region").string();
  config.chunk = number(value, "chunk");
  config.sweep = number(value, "sweep");
  config.interval = number(value, "interval");
  config.validate();
  return config;
}
std::string encoded_path(std::string_view path) {
  constexpr char alphabet[] = "0123456789ABCDEF";
  std::string result;
  for (char raw : path) {
    const auto c = static_cast<unsigned char>(raw);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
      result += static_cast<char>(c);
    else {
      result += '%';
      result += alphabet[c >> 4U];
      result += alphabet[c & 15U];
    }
  }
  return result;
}
class Destination {
public:
  explicit Destination(Config config) : config_(std::move(config)) {
    config_.validate();
    if (config_.kind == Config::Kind::local)
      directory(config_.target);
  }
  void get(std::string_view name, const fs::path &output) const {
    if (!digest_name(name, ".chunk") && !digest_name(name, ".bbm"))
      fail(ErrorCode::invalid_range);
    if (config_.kind == Config::Kind::local) {
      File input{fs::path{config_.target} / name, O_RDONLY};
      File out{output, O_WRONLY | O_CREAT | O_EXCL};
      std::array<std::byte, 65536> buffer{};
      for (;;) {
        const auto n = ::read(input.fd, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR)
          continue;
        if (n < 0)
          fail(ErrorCode::io, errno);
        if (n == 0)
          break;
        write_all(out.fd, ByteView{buffer}.first(static_cast<std::size_t>(n)));
      }
    } else if (config_.kind == Config::Kind::scp) {
      (void)process({executable({"BLACKBIRD_ARCHIVE_SCP", "scp"}), "-B", "-q", "-o",
                     "ConnectTimeout=10", config_.target + '/' + std::string{name},
                     output.string()});
    } else {
      if (curl(name, output, false) != "200")
        fail(ErrorCode::io);
    }
  }
  void put(const fs::path &input, const std::string &name) const {
    if (config_.kind == Config::Kind::local) {
      const auto output = fs::path{config_.target} / name;
      if (fs::exists(fs::symlink_status(output))) {
        if (hash(output) != hash(input))
          fail(ErrorCode::conflict);
        return;
      }
      // Copy onto the destination filesystem before its atomic, no-replace link.
      const auto temporary =
          output.string() + "." + std::to_string(::getpid()) + ".tmp";
      {
        File out{temporary, O_WRONLY | O_CREAT | O_EXCL};
        const auto data = read_all(input, 64 * 1024 * 1024);
        write_all(out.fd, data);
        if (::fsync(out.fd) != 0)
          fail(ErrorCode::io, errno);
      }
      if (::link(temporary.c_str(), output.c_str()) != 0) {
        const int error = errno;
        (void)::unlink(temporary.c_str());
        if (error != EEXIST || hash(output) != hash(input))
          fail(ErrorCode::io, error);
      } else
        (void)::unlink(temporary.c_str());
      sync_directory(config_.target);
    } else if (config_.kind == Config::Kind::scp) {
      std::array<char, 16> nonce{};
      if (::getentropy(nonce.data(), nonce.size()) != 0)
        fail(ErrorCode::io, errno);
      const auto temporary =
          "." + name + "." + hex({nonce.data(), nonce.size()}) + ".tmp";
      const auto [host, base] = scp_location();
      remote(host, "umask 077; mkdir -p -- " + shell_literal(base));
      (void)process({executable({"BLACKBIRD_ARCHIVE_SCP", "scp"}), "-B", "-q", "-o",
                     "ConnectTimeout=10", input.string(),
                     config_.target + '/' + temporary});
      const auto source = shell_literal(base + '/' + temporary);
      const auto target = shell_literal(base + '/' + name);
      // Never overwrite a published object, including after an interrupted retry.
      remote(host, "(ln -- " + source + ' ' + target + " 2>/dev/null || cmp -s -- " +
                       source + ' ' + target + ") && rm -- " + source);
    } else {
      const auto status = curl(name, input, true);
      if (status == "412") {
        const auto check = input.string() + ".existing";
        (void)::unlink(check.c_str());
        get(name, check);
        const bool equal = hash(check) == hash(input);
        (void)::unlink(check.c_str());
        if (!equal)
          fail(ErrorCode::conflict);
      } else if (status != "200")
        fail(ErrorCode::io);
    }
  }

private:
  std::pair<std::string, std::string> scp_location() const {
    if (config_.target.starts_with("scp://")) {
      const auto slash = config_.target.find('/', 6);
      return {"ssh://" + config_.target.substr(6, slash - 6),
              config_.target.substr(slash)};
    }
    const auto colon = config_.target.find(':');
    return {config_.target.substr(0, colon), config_.target.substr(colon + 1)};
  }
  void remote(const std::string &host, const std::string &command) const {
    (void)process({executable({"BLACKBIRD_ARCHIVE_SSH", "ssh"}), "-T", "-o",
                   "BatchMode=yes", "-o", "ConnectTimeout=10", "--", host, command});
  }
  std::string curl(std::string_view name, const fs::path &file, bool upload) const {
    const char *access = ::getenv("AWS_ACCESS_KEY_ID"),
               *secret = ::getenv("AWS_SECRET_ACCESS_KEY");
    if (!access || !secret || !*access || !*secret)
      fail(ErrorCode::invalid_range);
    const auto body = std::string_view{config_.target}.substr(5);
    const auto slash = body.find('/');
    const auto bucket = body.substr(0, slash);
    const auto prefix =
        slash == std::string_view::npos ? std::string_view{} : body.substr(slash + 1);
    const auto url =
        "https://s3." + config_.region + ".amazonaws.com/" + std::string{bucket} + '/' +
        (prefix.empty() ? "" : encoded_path(prefix) + '/') + std::string{name};
    std::string config = "url = " + quote(url) +
                         "\naws-sigv4 = " + quote("aws:amz:" + config_.region + ":s3") +
                         "\nuser = " + quote(std::string{access} + ':' + secret) +
                         "\noutput = " + quote(upload ? "/dev/null" : file.string()) +
                         "\nwrite-out = \"%{http_code}\"\n";
    if (const char *token = ::getenv("AWS_SESSION_TOKEN"); token && *token)
      config +=
          "header = " + quote(std::string{"x-amz-security-token: "} + token) + '\n';
    if (upload)
      config += "upload-file = " + quote(file.string()) +
                "\nheader = \"If-None-Match: *\"\nheader = " +
                quote("x-amz-checksum-sha256: " + checksum_base64(name.substr(0, 64))) +
                '\n';
    return process({executable({"BLACKBIRD_ARCHIVE_CURL", "curl"}), "--disable",
                    "--silent", "--show-error", "--proto", "=https",
                    "--connect-timeout", "10", "--max-time", "25", "--config", "-"},
                   config);
  }
  Config config_;
};
bool selected(const fs::path &relative) {
  const auto name = relative.generic_string();
  if (name == "audit")
    return true;
  if (relative.parent_path() == "diagnostics")
    return true;
  const auto base = relative.filename().string();
  return relative.begin() != relative.end() && *relative.begin() == "raw" &&
         std::distance(relative.begin(), relative.end()) == 3 &&
         (base == "descriptions" || base == "published" || base == "closed" ||
          base.starts_with("events.") || base.starts_with("seed.") ||
          base.starts_with("sealed."));
}
struct Work {
  fs::path path;
  explicit Work(const fs::path &parent) {
    std::string pattern = (parent / ".trace-archive-work-XXXXXX").string();
    if (!::mkdtemp(pattern.data()))
      fail(ErrorCode::io, errno);
    path = pattern;
  }
  ~Work() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};
std::uint64_t little(ByteView bytes) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i)
    value |= std::uint64_t{std::to_integer<unsigned char>(bytes[i])} << (8 * i);
  return value;
}
bool timestamp_equal(const struct stat &before, const struct stat &after) {
#ifdef __APPLE__
  return before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec &&
         before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
         before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec &&
         before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec;
#else
  return before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
         before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
         before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
         before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
#endif
}
std::string sweep(const fs::path &session, const Config &config) {
  File lease{session / "trace-archive.lock", O_RDWR | O_CREAT};
  if (::flock(lease.fd, LOCK_EX | LOCK_NB) != 0)
    fail(ErrorCode::busy);
  Work work{session};
  validate_target(config, session);
  Destination destination{config};
  std::set<std::string> known;
  std::map<std::string, Value> prior_files;
  std::string previous;
  const auto state_path = session / "trace-archive-state.bbm";
  if (fs::exists(state_path)) {
    const auto state = packet(state_path);
    const auto &stored_config = field(state, "config");
    if (field(stored_config, "target").string() == config.target &&
        field(stored_config, "region").string() == config.region) {
      previous = field(state, "manifest").string();
      for (const auto &file : field(state, "files").array()) {
        prior_files.emplace(field(file, "path").string(), file);
        for (const auto &chunk : field(file, "chunks").array())
          known.insert(field(chunk, "object").string());
      }
    }
  }
  std::vector<fs::path> files;
  struct RawPrefix {
    std::vector<std::byte> publication;
    std::uint64_t segment, end;
    bool closed;
  };
  std::map<fs::path, RawPrefix> raw;
  const auto raw_directory = session / "raw";
  const auto diagnostics = session / "diagnostics";
  if (fs::exists(fs::symlink_status(diagnostics)) &&
      !fs::is_directory(fs::symlink_status(diagnostics)))
    fail(ErrorCode::invalid_range);
  if (fs::exists(fs::symlink_status(raw_directory))) {
    if (!fs::is_directory(fs::symlink_status(raw_directory)))
      fail(ErrorCode::invalid_range);
    for (const auto &entry : fs::directory_iterator{raw_directory}) {
      if (!fs::is_directory(entry.symlink_status()))
        fail(ErrorCode::invalid_range);
      const auto published = entry.path() / "published";
      const bool closed = fs::exists(entry.path() / "closed");
      if (!fs::exists(published))
        continue; // A bootstrap run has no recorder-published prefix yet.
      auto bytes = read_all(published, 40);
      if (bytes.size() != 40)
        fail(ErrorCode::corrupt);
      const auto segment = little(ByteView{bytes}.first(8));
      const auto end = little(ByteView{bytes}.subspan(32, 8));
      if (end < journal_header_size)
        fail(ErrorCode::corrupt);
      raw.emplace(entry.path().lexically_relative(session),
                  RawPrefix{std::move(bytes), segment, end, closed});
    }
  }
  for (const auto &entry : fs::recursive_directory_iterator{session}) {
    const auto relative = entry.path().lexically_relative(session);
    if (selected(relative)) {
      if (!fs::is_regular_file(entry.symlink_status()))
        fail(ErrorCode::invalid_range);
      if (*relative.begin() == "raw") {
        const auto prefix = raw.find(relative.parent_path());
        if (prefix == raw.end())
          continue;
        const auto name = relative.filename().string();
        if (name == "closed" && !prefix->second.closed)
          continue;
        if (name.starts_with("events.") &&
            integer(std::string_view{name}.substr(7)) > prefix->second.segment)
          continue;
        if ((name.starts_with("seed.") || name.starts_with("sealed.")) &&
            integer(std::string_view{name}.substr(name.find('.') + 1)) >
                prefix->second.segment)
          continue;
      }
      files.push_back(relative);
    }
  }
  std::sort(files.begin(), files.end());
  Value::Array rows;
  std::vector<std::byte> buffer(static_cast<std::size_t>(config.chunk));
  for (const auto &relative : files) {
    const auto basename = relative.filename().string();
    const auto prefix = raw.find(relative.parent_path());
    const bool publication = prefix != raw.end() && basename == "published";
    const auto original_path = session / relative;
    const auto input_path = publication ? work.path / "publication" : original_path;
    if (publication)
      atomic_file(input_path, prefix->second.publication);
    File file{input_path, O_RDONLY};
    struct stat before{}, after{};
    if (::fstat(file.fd, &before) != 0 || before.st_size < 0)
      fail(ErrorCode::io, errno);
    auto length = static_cast<std::uint64_t>(before.st_size);
    if (prefix != raw.end() &&
        basename == "events." + std::to_string(prefix->second.segment)) {
      if (length < prefix->second.end)
        fail(ErrorCode::incomplete);
      length = prefix->second.end;
    }
    std::uint64_t offset = 0;
    Value::Array chunks;
    while (offset < length) {
      const auto count =
          static_cast<std::size_t>(std::min(config.chunk, length - offset));
      std::size_t received = 0;
      while (received < count) {
        const auto n = ::pread(file.fd, buffer.data() + received, count - received,
                               static_cast<off_t>(offset + received));
        if (n < 0 && errno == EINTR)
          continue;
        if (n <= 0)
          fail(ErrorCode::incomplete);
        received += static_cast<std::size_t>(n);
      }
      const auto staged = work.path / "chunk";
      atomic_file(staged, ByteView{buffer}.first(count));
      const auto name = hash(staged) + ".chunk";
      if (!known.contains(name)) {
        destination.put(staged, name);
        known.insert(name);
      }
      chunks.push_back(Value::object({{"object", Value{name}}, {"bytes", num(count)}}));
      offset += count;
    }
    const bool append_only = relative == "audit" || basename.starts_with("events.");
    if (::fstat(file.fd, &after) != 0 || after.st_size < before.st_size ||
        (before.st_size == after.st_size && !timestamp_equal(before, after)) ||
        (!append_only &&
         (before.st_size != after.st_size || !timestamp_equal(before, after))))
      fail(ErrorCode::busy);
    rows.push_back(Value::object({{"path", Value{relative.generic_string()}},
                                  {"bytes", num(length)},
                                  {"chunks", Value{std::move(chunks)}}}));
    prior_files.erase(relative.generic_string());
  }
  // Expired/removed local diagnostics remain part of the long-term archive.
  for (const auto &[path, file] : prior_files) {
    (void)path;
    rows.push_back(file);
  }
  std::sort(rows.begin(), rows.end(), [](const Value &a, const Value &b) {
    return field(a, "path").string() < field(b, "path").string();
  });
  const auto manifest = Value::object(
      {{"version", num(1)},
       {"scope",
        Value{"observed-originals; no acknowledgement or settlement inferred"}},
       {"files", Value{std::move(rows)}}});
  const auto staged = work.path / "manifest";
  packet_file(staged, manifest);
  const auto name = hash(staged) + ".bbm";
  if (name != previous)
    destination.put(staged, name);
  packet_file(state_path, Value::object({{"config", config.value()},
                                         {"manifest", Value{name}},
                                         {"files", field(manifest, "files")}}));
  return name;
}
void restore(const Config &config, const std::string &name, const fs::path &output) {
  if (!digest_name(name, ".bbm") || fs::exists(fs::symlink_status(output)))
    fail(ErrorCode::invalid_range);
  directory(output.parent_path());
  Work work{output.parent_path()};
  Destination destination{config};
  const auto manifest_path = work.path / "manifest";
  destination.get(name, manifest_path);
  if (hash(manifest_path) + ".bbm" != name)
    fail(ErrorCode::corrupt);
  const auto manifest = packet(manifest_path);
  if (number(manifest, "version") != 1)
    fail(ErrorCode::unsupported);
  const auto tree = work.path / "tree";
  directory(tree);
  std::set<std::string> paths;
  for (const auto &row : field(manifest, "files").array()) {
    const auto &path = field(row, "path").string();
    const fs::path relative{path};
    if (relative.is_absolute() || relative.empty() ||
        relative.lexically_normal() != relative ||
        path.find('\0') != std::string::npos || !selected(relative) ||
        !paths.insert(path).second)
      fail(ErrorCode::invalid_range);
    for (const auto &component : relative)
      if (component == ".." || component == ".")
        fail(ErrorCode::invalid_range);
    const auto target = tree / relative;
    directory(target.parent_path());
    File file{target, O_WRONLY | O_CREAT | O_EXCL};
    std::uint64_t length = 0;
    for (const auto &chunk : field(row, "chunks").array()) {
      const auto &object = field(chunk, "object").string();
      const auto bytes = number(chunk, "bytes");
      if (!digest_name(object, ".chunk") || bytes == 0 || bytes > 64 * 1024 * 1024 ||
          bytes > number(row, "bytes") || length > number(row, "bytes") - bytes)
        fail(ErrorCode::invalid_range);
      const auto downloaded = work.path / "chunk";
      (void)::unlink(downloaded.c_str());
      destination.get(object, downloaded);
      if (fs::file_size(downloaded) != bytes || hash(downloaded) + ".chunk" != object)
        fail(ErrorCode::corrupt);
      write_all(file.fd, read_all(downloaded, 64 * 1024 * 1024));
      length += bytes;
    }
    if (length != number(row, "bytes"))
      fail(ErrorCode::incomplete);
    if (::fsync(file.fd) != 0)
      fail(ErrorCode::io, errno);
  }
  // Publish only a complete tree. Never turn restored data into running effects.
  if (fs::exists(fs::symlink_status(output)))
    fail(ErrorCode::conflict);
#ifdef __APPLE__
  const int renamed =
      ::renameatx_np(AT_FDCWD, tree.c_str(), AT_FDCWD, output.c_str(), RENAME_EXCL);
#else
  const long renamed = ::syscall(SYS_renameat2, AT_FDCWD, tree.c_str(), AT_FDCWD,
                                 output.c_str(), RENAME_NOREPLACE);
#endif
  if (renamed != 0)
    fail(ErrorCode::io, errno);
  sync_directory(output.parent_path());
}
std::uint64_t footprint(const fs::path &session) {
  std::uint64_t total = 0;
  for (const auto &entry : fs::recursive_directory_iterator{session})
    if (selected(entry.path().lexically_relative(session)) && entry.is_regular_file()) {
      const auto size = entry.file_size();
      if (size > UINT64_MAX - total)
        fail(ErrorCode::overflow);
      total += size;
    }
  return total;
}
int run(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: blackbird-archive configure SESSION DEST [--chunk-bytes N "
                 "--sweep-bytes N --interval-seconds N --region REGION] | sweep "
                 "SESSION | watch SESSION | status SESSION | restore DEST MANIFEST "
                 "DIRECTORY [--region REGION]\n");
    return 2;
  }
  const std::string_view operation{argv[1]};
  if (operation == "configure" || operation == "restore") {
    const bool restoring = operation == "restore";
    const int first_option = restoring ? 5 : 4;
    if (argc < first_option || (argc - first_option) % 2 != 0)
      fail(ErrorCode::invalid_range);
    Config config;
    config.target = argv[restoring ? 2 : 3];
    for (int i = first_option; i < argc; i += 2) {
      const std::string_view option{argv[i]};
      if (option == "--region")
        config.region = argv[i + 1];
      else if (option == "--chunk-bytes")
        config.chunk = integer(argv[i + 1]);
      else if (option == "--sweep-bytes")
        config.sweep = integer(argv[i + 1]);
      else if (option == "--interval-seconds")
        config.interval = integer(argv[i + 1]);
      else
        fail(ErrorCode::invalid_range);
    }
    if (config.region.empty())
      if (const char *region = ::getenv("AWS_REGION"))
        config.region = region;
    if (config.region.empty())
      if (const char *region = ::getenv("AWS_DEFAULT_REGION"))
        config.region = region;
    config.validate();
    if (restoring)
      restore(config, argv[3], fs::absolute(argv[4]));
    else {
      directory(argv[2]);
      validate_target(config, fs::absolute(argv[2]));
      packet_file(fs::path{argv[2]} / "trace-archive.bbm", config.value());
    }
    return 0;
  }
  if (argc != 3)
    fail(ErrorCode::invalid_range);
  const auto session = fs::absolute(argv[2]);
  const auto config = configuration(session);
  if (operation == "sweep") {
    std::puts(sweep(session, config).c_str());
    return 0;
  }
  if (operation == "status") {
    const auto state = packet(session / "trace-archive-state.bbm");
    std::printf(
        "last completed manifest: %s\nfiles: %zu\nsemantic session cap unchanged\n",
        field(state, "manifest").string().c_str(),
        field(state, "files").array().size());
    return 0;
  }
  if (operation != "watch")
    fail(ErrorCode::invalid_range);
  std::uint64_t last_size = 0;
  auto last = detail::Clock::time_point{};
  bool stopping = false;
  do {
    try {
      const auto current = footprint(session);
      const auto now = detail::Clock::now();
      if (stopping || last == detail::Clock::time_point{} || current < last_size ||
          current - last_size >= config.sweep ||
          now - last >= std::chrono::seconds{config.interval}) {
        const auto manifest = sweep(session, config);
        std::fprintf(stderr, "trace archive: preserved %s\n", manifest.c_str());
        last_size = current;
        last = now;
      }
    } catch (const Error &error) {
      std::fprintf(stderr, "trace archive: %s (%lld); local originals retained\n",
                   error_name(error.code), static_cast<long long>(error.detail));
    } catch (const std::exception &) {
      std::fprintf(stderr, "trace archive: filesystem or allocation failure; local "
                           "originals retained\n");
    }
    if (!stopping) {
      pollfd input{STDIN_FILENO, POLLIN, 0};
      const auto result = ::poll(&input, 1, 1000);
      if (result < 0 && errno != EINTR)
        fail(ErrorCode::io, errno);
      if (result > 0) {
        char byte;
        if ((input.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 ||
            ::read(STDIN_FILENO, &byte, 1) == 0)
          stopping = true;
      }
    } else
      return 0;
  } while (true);
}
} // namespace
int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const blackbird::Error &error) {
    std::fprintf(stderr, "trace archive: %s (%lld)\n",
                 blackbird::error_name(error.code),
                 static_cast<long long>(error.detail));
  } catch (const std::exception &) {
    std::fprintf(stderr,
                 "trace archive: invalid configuration or filesystem failure\n");
  }
  return 1;
}
