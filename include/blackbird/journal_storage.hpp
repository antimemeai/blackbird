#pragma once

#include "blackbird/foundation.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace blackbird {

enum class FileAccess : std::uint8_t { read_only, read_write };
class JournalFile : public Storage {
public:
  virtual Result<void> lock_writer() = 0;
  // Memory adapters have no lease; descriptor decorators must forward release.
  // Reader references retain storage ownership, not writer ownership.
  virtual void release_writer() noexcept {}
};
class DiagnosticStore;
struct ScratchCleanup {
  std::size_t scanned = 0, removed = 0;
  bool complete = false;
  std::optional<Error> error;
};
class JournalDirectory {
public:
  virtual ~JournalDirectory() = default;
  virtual Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) = 0;
  virtual Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                             FileAccess access) = 0;
  virtual Result<void> synchronize_directory(SyncStrength strength) = 0;
  // Caller must hold this journal's exclusive writer lease, with no publication
  // in flight. Only reserved numeric publication scratch names are eligible.
  virtual Result<ScratchCleanup>
  reclaim_publication_scratch(std::string_view, std::size_t = 128, std::size_t = 16) {
    return Result<ScratchCleanup>::failure({ErrorCode::unsupported});
  }
  virtual Result<void> store_diagnostic(std::string_view, ByteView, std::uint64_t) {
    return Result<void>::failure({ErrorCode::unsupported});
  }
  virtual Result<std::vector<std::byte>> read_diagnostic(std::string_view,
                                                         std::uint64_t) {
    return Result<std::vector<std::byte>>::failure({ErrorCode::unsupported});
  }
  virtual Result<ScratchCleanup> expire_diagnostics(std::uint64_t) {
    return Result<ScratchCleanup>::failure({ErrorCode::unsupported});
  }
  virtual Result<void> replace_file(std::string_view, std::string_view) {
    return Result<void>::failure({ErrorCode::unsupported});
  }
};
class NativeJournalFile final : public JournalFile {
public:
  ~NativeJournalFile() override;
  NativeJournalFile(const NativeJournalFile &) = delete;
  NativeJournalFile &operator=(const NativeJournalFile &) = delete;
  NativeJournalFile(NativeJournalFile &&other) noexcept;
  NativeJournalFile &operator=(NativeJournalFile &&other) noexcept;
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView output) override;
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override;
  Result<std::uint64_t> extent() override;
  Result<void> synchronize(SyncStrength strength) override;
  Result<void> lock_writer() override;
  void release_writer() noexcept override;

private:
  friend class NativeJournalDirectory;
  explicit NativeJournalFile(int descriptor) : descriptor_(descriptor) {}
  int descriptor_;
};
class NativeJournalDirectory final : public JournalDirectory {
public:
  static Result<NativeJournalDirectory> open(std::string_view path);
  // Opens one child relative to a caller-held directory. No symlink traversal.
  static Result<NativeJournalDirectory> open_at(int directory, std::string_view name);
  // Caller owns the returned read-only directory fd. The child must share the
  // owner; its mode may be wider inside this already-private directory.
  Result<int> open_child_directory(std::string_view name) const;
  ~NativeJournalDirectory() override;
  NativeJournalDirectory(const NativeJournalDirectory &) = delete;
  NativeJournalDirectory &operator=(const NativeJournalDirectory &) = delete;
  NativeJournalDirectory(NativeJournalDirectory &&other) noexcept;
  NativeJournalDirectory &operator=(NativeJournalDirectory &&other) noexcept;
  Result<std::unique_ptr<JournalFile>> create_exclusive(std::string_view name) override;
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override;
  Result<void> synchronize_directory(SyncStrength strength) override;
  Result<void> replace_file(std::string_view from, std::string_view to) override;
  Result<void> store_diagnostic(std::string_view name, ByteView bytes,
                                std::uint64_t now) override;
  Result<std::vector<std::byte>> read_diagnostic(std::string_view name,
                                                 std::uint64_t now) override;
  Result<ScratchCleanup> expire_diagnostics(std::uint64_t now) override;
  Result<ScratchCleanup>
  reclaim_publication_scratch(std::string_view journal, std::size_t max_scan = 128,
                              std::size_t max_remove = 16) override;

public:
  const ScratchCleanup &scratch_cleanup_status() const noexcept {
    return cleanup_status_;
  }

private:
  Result<ScratchCleanup> reclaim_scratch_pass(std::string_view journal,
                                              std::size_t max_scan,
                                              std::size_t max_remove);
  void *cleanup_stream_ = nullptr;
  std::string cleanup_journal_;
  ScratchCleanup cleanup_status_;
  explicit NativeJournalDirectory(int descriptor) : descriptor_(descriptor) {}
  Result<std::unique_ptr<JournalFile>> open_file(std::string_view name, int flags);
  int descriptor_;
  std::shared_ptr<DiagnosticStore> diagnostics_;
};

} // namespace blackbird
