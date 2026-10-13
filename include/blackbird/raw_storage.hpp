#pragma once

#include "blackbird/local_timing.hpp"
#include "blackbird/raw_provider.hpp"

#include <atomic>
#include <dirent.h>
#include <memory>
#include <mutex>
#include <sys/stat.h>

namespace blackbird {
class JournalFile;
class RawStorageAuthority;
class RawStorageCleanupState;
class RawRetainedOwnerEnd;
class RawRetainedSettlementEnd;
class RawStorageResource;
class RawStorageSettlementLoan;
class RawStorageLoanScope;
class RawStorageLoanPause;
class RawStorageHandoff;
class RawStorageCall;
class RawStorageDiagnosticUnlink;
struct RawStorageLoanBudget;
struct RawStorageWriteContinuation;

inline constexpr std::uint64_t raw_storage_io_limit = 16 * 1024 * 1024;

enum class RawStorageActionKind : std::uint8_t {
  open = 1,
  open_at,
  create_exclusive,
  open_existing,
  read_at,
  write_at,
  extent,
  synchronize,
  lock_writer,
  release_writer,
  synchronize_directory,
  replace_file,
  store_diagnostic,
  read_diagnostic,
  expire_diagnostics,
  reclaim_scratch
};
enum class RawStorageLoanKind : std::uint8_t {
  stop = 1,
  joined_terminal = 2,
  selected_exec = 3,
  adapter_receipt = 4,
  adapter_terminal = 5
};
struct RawStorageLoanBudget {
  RawStorageLoanKind kind = RawStorageLoanKind::stop;
  std::uint64_t write_bytes = 0;
  std::size_t batches = 0, submissions = 0, catalog_finds = 0;
  std::uint64_t catalog_maximum = 0;
  std::uint64_t positive_read_bytes = 0, positive_read_calls = 0;
};
struct RawStorageLoanReservation {
  std::uint64_t control = 0, data = 0;
  std::uint64_t write_calls = 0, probes = 0, scopes = 0;
};
Result<RawStorageLoanReservation>
raw_storage_loan_reservation(const RawStorageLoanBudget &budget) noexcept;

class RawProductStorageOwner {
public:
  static Result<RawProductStorageOwner> prepare(std::optional<RawEventRef> parent = {});
  RawProductStorageOwner() = default;
  bool enabled() const noexcept { return bool(authority_); }
  RawEventRef reference() const noexcept;
  bool permits_current() const noexcept;
  bool permits_thread() const noexcept;
  // Read permission only; ordinary admission or actual loan eligibility follows.
  bool permits_origin_read() const noexcept;
  Result<std::unique_ptr<RawStorageHandoff>>
  prepare_handoff(RawThreadHandle child, RawWorkerDispatch dispatch) const;
  Result<std::unique_ptr<RawStorageSettlementLoan>>
  prepare_joined_loan(RawThreadHandle child, const void *root,
                      RawStorageLoanBudget budget,
                      std::optional<RawEventRef> parent = {}) const;
  const std::shared_ptr<RawStorageAuthority> &authority() const noexcept {
    return authority_;
  }

private:
  friend class RawStorageResource;
  friend class RawRetainedOwnerEnd;
  friend class RawRetainedSettlementEnd;
  friend class RawStorageHandoff;
  explicit RawProductStorageOwner(std::shared_ptr<RawStorageAuthority> authority)
      : authority_(std::move(authority)) {}
  std::shared_ptr<RawStorageAuthority> authority_;
};

// The grant, not a current TLS binding, authorizes the selected perform caller.
class RawStorageHandoff {
public:
  ~RawStorageHandoff();
  RawStorageHandoff(const RawStorageHandoff &) = delete;
  RawStorageHandoff &operator=(const RawStorageHandoff &) = delete;
  Result<std::unique_ptr<RawStorageSettlementLoan>>
  prepare_loan(const void *root, RawStorageLoanBudget budget,
               std::optional<RawEventRef> parent = {});
  void enter(RawEventRef command);
  void finish(std::optional<Error> error = {}) noexcept;

private:
  friend class RawProductStorageOwner;
  RawStorageHandoff(std::shared_ptr<RawStorageAuthority> authority,
                    RawThreadHandle child);
  std::shared_ptr<RawStorageAuthority> authority_;
  RawThreadHandle child_;
  std::optional<RawControlToken> bank_;
  RawEventRef preparation_, entry_;
  bool active_ = false, finished_ = false, owns_pending_ = false;
};

class RawStorageAuthorityScope {
public:
  RawStorageAuthorityScope(RawStorageHandoff *handoff, RawEventRef command);
  ~RawStorageAuthorityScope();
  RawStorageAuthorityScope(const RawStorageAuthorityScope &) = delete;
  RawStorageAuthorityScope &operator=(const RawStorageAuthorityScope &) = delete;
  void error(Error value) noexcept { error_ = value; }

private:
  RawStorageHandoff *handoff_;
  std::optional<Error> error_;
};

// No naked cleanup claim escapes this wrapper: every held_ mutation is serialized.
class RawStorageCleanupBank {
public:
  RawStorageCleanupBank() = default;
  ~RawStorageCleanupBank();
  RawStorageCleanupBank(RawStorageCleanupBank &&other) noexcept;
  RawStorageCleanupBank &operator=(RawStorageCleanupBank &&other) noexcept;
  RawStorageCleanupBank(const RawStorageCleanupBank &) = delete;
  RawStorageCleanupBank &operator=(const RawStorageCleanupBank &) = delete;
  bool funded() const noexcept { return credit_.has_value(); }

private:
  friend class RawStorageResource;
  friend class RawStorageCleanupState;
  friend class RawRetainedOwnerEnd;
  friend class RawRetainedSettlementEnd;
  static RawStorageCleanupBank
  prepare(const std::shared_ptr<RawStorageCleanupState> &state, std::uint64_t bytes);
  void reset() noexcept;
  std::shared_ptr<RawStorageCleanupState> state_;
  std::optional<RawControlToken> credit_;
};

class RawStorageResource {
public:
  static std::unique_ptr<RawStorageResource>
  prepare(const RawProductStorageOwner &owner, bool stream = false);
  ~RawStorageResource();
  RawStorageResource(const RawStorageResource &) = delete;
  RawStorageResource &operator=(const RawStorageResource &) = delete;
  RawEventRef reference() const noexcept { return reference_; }
  RawProductStorageOwner owner() const;
  void attach(int descriptor, RawEventRef reference, RawEventRef action) noexcept;
  void stream(DIR *value) noexcept;
  void close() noexcept;
  void prepare_release();
  void acquired_writer(bool success) noexcept;
  void release_writer() noexcept;
  void begin_write(std::size_t bytes, RawStorageWriteContinuation &continuation);

private:
  friend class RawStorageAction;
  friend class RawStorageNative;
  friend class RawStorageCall;
  friend class RawStorageLoanScope;
  friend class RawStorageLoanPause;
  friend class RawStorageSettlementLoan;
  explicit RawStorageResource(std::shared_ptr<RawStorageAuthority> authority);
  std::shared_ptr<RawStorageAuthority> authority_;
  RawStorageCleanupBank close_, release_;
  RawEventRef reference_, action_;
  int descriptor_ = -1;
  DIR *stream_ = nullptr;
  enum class Lease : std::uint8_t { unheld, held, unlock_failed };
  Lease lease_ = Lease::unheld;
  std::optional<Error> unlock_error_;
  RawStorageLoanScope *scope_ = nullptr;
};

struct RawStorageWriteContinuation {
  RawStorageResource *resource = nullptr;
  std::optional<RawControlToken> slots;
  std::uint64_t remaining_calls = 0;
  RawEventRef input;
  ByteView published;
  std::uint64_t offset = 0;
};

// Stack-owned action. Its native return claims are acquired before each syscall.
class RawStorageAction {
public:
  RawStorageAction(const RawProductStorageOwner &owner, RawStorageActionKind kind,
                   RawStorageResource *resource = nullptr,
                   std::span<const ByteView> arguments = {},
                   RawStorageWriteContinuation *continuation = nullptr);
  ~RawStorageAction();
  RawStorageAction(const RawStorageAction &) = delete;
  RawStorageAction &operator=(const RawStorageAction &) = delete;
  RawEventRef reference() const noexcept { return reference_; }
  bool enabled() const noexcept { return bool(authority_); }
  void operand(std::uint8_t kind, ByteView value);
  void finish(std::optional<Error> error = {}) noexcept;
  void use(RawStorageResource *resource);

private:
  friend class RawStorageNative;
  friend class RawStorageCall;
  friend class RawStorageDiagnosticUnlink;
  std::shared_ptr<RawStorageAuthority> authority_;
  RawStorageResource *resource_;
  RawStorageSettlementLoan *loan_ = nullptr;
  RawEventRef reference_;
  std::optional<RawControlToken> credit_, open_credit_;
  RawCapacityClaim *data_ = nullptr;
  bool finished_ = false;
  std::uint64_t native_read_limit_ = UINT64_MAX;
  RawStorageWriteContinuation *continuation_ = nullptr;
};

// One diagnostic failure cleanup, admitted before creating that file. The
// CString remains owned by the caller; successful storage emits no unlink.
class RawStorageDiagnosticUnlink {
public:
  RawStorageDiagnosticUnlink(RawStorageAction &action, int directory,
                             std::string_view cstring);
  ~RawStorageDiagnosticUnlink();
  RawStorageDiagnosticUnlink(const RawStorageDiagnosticUnlink &) = delete;
  RawStorageDiagnosticUnlink &operator=(const RawStorageDiagnosticUnlink &) = delete;
  void run() noexcept;

private:
  std::shared_ptr<RawStorageAuthority> authority_;
  RawThreadHandle issuer_;
  RawEventRef action_, resource_;
  int directory_;
  std::string_view name_;
  std::optional<RawControlToken> control_;
  std::optional<RawCapacityClaim> data_;
  bool reached_ = false;
};

struct RawStorageSelection {
  const void *root = nullptr;
  RawProductStorageOwner owner;
  std::shared_ptr<JournalFile> journal, archive;
};
class RawStorageSettlementLoan {
public:
  static Result<std::unique_ptr<RawStorageSettlementLoan>>
  prepare(const void *root, const RawProductStorageOwner &owner,
          RawStorageLoanBudget budget, std::optional<RawEventRef> parent = {});
  ~RawStorageSettlementLoan();
  RawStorageSettlementLoan(const RawStorageSettlementLoan &) = delete;
  RawStorageSettlementLoan &operator=(const RawStorageSettlementLoan &) = delete;
  // Only the actual joined/never-started caller lifecycle may change this state.
  void started() noexcept;
  void joined() noexcept;
  void finish(std::optional<Error> error = {}) noexcept;
  RawEventRef reference() const noexcept { return reference_; }

private:
  friend class RawStorageAction;
  friend class RawStorageNative;
  friend class RawStorageLoanScope;
  friend class RawStorageLoanPause;
  friend class RawStorageResource;
  friend class RawProductStorageOwner;
  friend class RawStorageHandoff;
  static Result<std::unique_ptr<RawStorageSettlementLoan>>
  prepare_authorized(const void *root, const RawProductStorageOwner &owner,
                     RawStorageLoanBudget budget, std::optional<RawEventRef> parent);
  RawStorageSettlementLoan(const void *root, RawProductStorageOwner owner,
                           RawStorageLoanBudget budget);
  std::optional<RawControlToken> action(RawStorageActionKind kind, bool catalog,
                                        RawCapacityClaim *&data,
                                        std::uint64_t &read_limit);
  const void *root_;
  RawProductStorageOwner owner_;
  RawThreadHandle issuer_;
  const void *issuing_thread_ = nullptr;
  pid_t issuing_process_ = 0;
  RawStorageLoanBudget budget_;
  RawEventRef reference_;
  std::optional<RawControlToken> control_, lifecycle_;
  std::optional<RawCapacityClaim> writes_, catalog_, reads_;
  std::uint64_t extents_ = 0, syncs_ = 0, write_calls_ = 0;
  std::uint64_t catalog_calls_ = 0, read_calls_ = 0, read_bytes_ = 0, scopes_ = 0;
  std::uint64_t batches_ = 0;
  std::uint64_t retargets_ = 0;
  bool joined_ = false, finished_ = false, active_ = false;
};

class RawStorageLoanScope {
public:
  RawStorageLoanScope(RawStorageSettlementLoan &loan, RawStorageSelection selection);
  ~RawStorageLoanScope();
  RawStorageLoanScope(const RawStorageLoanScope &) = delete;
  RawStorageLoanScope &operator=(const RawStorageLoanScope &) = delete;
  void error(Error value) noexcept { error_ = value; }

private:
  friend class RawStorageAction;
  friend class RawStorageNative;
  friend class RawStorageResource;
  friend class RawStorageLoanPause;
  RawStorageSettlementLoan &loan_;
  RawStorageSelection selection_;
  RawStorageResource *journal_ = nullptr, *archive_ = nullptr;
  std::optional<RawControlToken> bank_;
  RawEventRef reference_;
  std::optional<Error> error_;
  bool paused_ = false;
};

// Ordinary optional maintenance acquires on its actual caller, not this finite
// cleanup grant. Restore names the files really selected after the callback.
class RawStorageLoanPause {
public:
  explicit RawStorageLoanPause(const RawStorageSelection &selection);
  ~RawStorageLoanPause();
  RawStorageLoanPause(const RawStorageLoanPause &) = delete;
  RawStorageLoanPause &operator=(const RawStorageLoanPause &) = delete;
  Result<void> restore(RawStorageSelection selection, std::uint64_t catalog_entries);

private:
  RawStorageLoanScope *scope_ = nullptr;
  std::optional<RawControlToken> bank_;
};

// Typed native operations. No observer/callback or recorder recursion.
class RawStorageNative {
public:
  static int open(RawStorageAction &action, RawStorageResource *target,
                  std::optional<int> directory, std::string_view cstring, int flags,
                  std::optional<mode_t> mode = {});
  static ssize_t read(RawStorageAction &action, int descriptor, MutableByteView output,
                      std::optional<off_t> offset = {});
  static ssize_t write(RawStorageAction &action, int descriptor, ByteView input,
                       std::optional<off_t> offset = {});
  static int stat(RawStorageAction &action, int descriptor, struct stat &output);
  static int stat_at(RawStorageAction &action, int directory, std::string_view cstring,
                     struct stat &output, int flags);
  static uid_t uid(RawStorageAction &action);
  static int sync(RawStorageAction &action, int descriptor, SyncStrength strength);
  static int lock(RawStorageAction &action, int descriptor, int operation);
  static int rename(RawStorageAction &action, int from_directory, std::string_view from,
                    int to_directory, std::string_view to);
  static int unlink(RawStorageAction &action, int directory, std::string_view cstring,
                    int flags);
  static int mkdir(RawStorageAction &action, int directory, std::string_view cstring,
                   mode_t mode);
  static DIR *directory(RawStorageAction &action, RawStorageResource *target,
                        int descriptor);
  static dirent *next(RawStorageAction &action, DIR *stream);
};
std::string_view raw_storage_description() noexcept;
} // namespace blackbird
