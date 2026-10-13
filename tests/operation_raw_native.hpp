#pragma once
#include "blackbird/local_timing.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <unwind.h>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
#endif

// Fixture-only observation forwards real native calls. Selected cuts occur
// after the real target rename or a complete literal live encoder allocation.
namespace operation_cut {
struct Allocation {
  void *pointer{};
  std::size_t bytes{};
};
thread_local std::array<Allocation, 4096> live{};
thread_local const char *target = nullptr;
thread_local std::string_view literal;
thread_local bool ready = false, watching = false, renamed = false, refused = false;
thread_local bool allocation_mode = true, receipt_ready = false, receipt_error = false;
thread_local std::size_t acquired_extent = 0, refused_size = 0;
thread_local dev_t journal_device{};
thread_local ino_t journal_inode{};
thread_local int receipt_descriptor = -1;
thread_local off_t receipt_next = 0;
thread_local ssize_t receipt_short = 0;
thread_local std::array<std::byte, 68> actual_receipt{};
struct ClockFact {
  clockid_t method;
  int result, error;
  timespec value;
};
thread_local std::array<ClockFact, 4096> clocks{};
thread_local std::size_t clock_count = 0;
thread_local bool clock_watch = false, clock_failure = false;
inline bool readable(const void *pointer, std::size_t size) noexcept {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  if (pointer && __asan_region_is_poisoned(const_cast<void *>(pointer), size))
    return false;
#endif
#endif
  return pointer != nullptr && size > 0;
}
// Native setup probe reads the source-owned channel ABI, never an emission callback.
struct alignas(64) ProbeWord {
  std::atomic<std::uint64_t> value;
};
struct ProbePartition {
  ProbeWord published, acknowledged;
};
struct ProbeHeader {
  ProbeWord ready;
  blackbird::IdentityBytes epoch;
  blackbird::RawCapacities capacities;
  std::array<std::uint64_t, 3> offsets;
  std::uint64_t mapped_bytes;
  ProbeWord data_armed, space_armed;
  std::array<ProbePartition, 3> partitions;
};
thread_local std::array<char, 32> lane_name{};
thread_local blackbird::RawCapacities lane_capacity{};
thread_local blackbird::IdentityBytes lane_epoch{};
thread_local bool register_watch = false, require_result_phase = false;
thread_local ProbeHeader *probe = nullptr;
thread_local std::size_t probe_size = 0;
thread_local bool result_phase_seen = false;
thread_local int rename_cancel = -1, space_cancel = -1;
thread_local int cancellation_watch = -1;
thread_local bool space_wait = false;
thread_local int publication_notice = -1, publication_release = -1;
inline void notify(int descriptor) noexcept {
  const char byte = 1;
  const iovec part{const_cast<char *>(&byte), 1};
  if (::writev(descriptor, &part, 1) != 1)
    std::abort();
}
thread_local unsigned diagnostic_cut = 0, diagnostic_writes = 0;
thread_local bool open_cut = false, open_refused = false;
thread_local bool diagnostic_failed = false, append_loan_paused = false;
thread_local bool dispatch_copy_cut = false, dispatch_copy_refused = false;
struct DispatchStack {
  bool dispatch = false, attempt = false;
};
inline _Unwind_Reason_Code dispatch_frame(_Unwind_Context *frame,
                                          void *opaque) noexcept {
  auto &stack = *static_cast<DispatchStack *>(opaque);
  Dl_info info{};
  const auto address = _Unwind_GetIP(frame);
  if (address && ::dladdr(reinterpret_cast<const void *>(address - 1), &info) &&
      info.dli_sname) {
    const std::string_view name{info.dli_sname};
    stack.dispatch = stack.dispatch || name.find("RetainedState8dispatch") != name.npos;
    stack.attempt = stack.attempt || name.find("RetainedState7attempt") != name.npos;
  }
  return _URC_NO_REASON;
}
inline bool dispatch_checkpoint_copy(std::size_t bytes) noexcept {
  // This fixture admits exactly one InvocationId. Its checkpoint vector copy
  // allocates that actual field width; attempt's owned-input copies are excluded.
  if (bytes != sizeof(blackbird::InvocationId))
    return false;
  DispatchStack stack;
  (void)_Unwind_Backtrace(dispatch_frame, &stack);
  return stack.dispatch && !stack.attempt;
}
inline void probe_open() {
  const int descriptor = ::shm_open(lane_name.data(), O_RDONLY, 0);
  if (descriptor < 0)
    std::abort();
  struct stat info{};
  if (::fstat(descriptor, &info) != 0)
    std::abort();
  probe_size = static_cast<std::size_t>(info.st_size);
  void *mapping = ::mmap(nullptr, probe_size, PROT_READ, MAP_SHARED, descriptor, 0);
  ::close(descriptor);
  if (mapping == MAP_FAILED)
    std::abort();
  probe = static_cast<ProbeHeader *>(mapping);
}
inline void probe_close() noexcept {
  if (probe)
    ::munmap(probe, probe_size);
  probe = nullptr;
}
inline bool result_phase() noexcept {
  if (!probe)
    return false;
  const auto end = probe->partitions[1].published.value.load(std::memory_order_acquire);
  if (end > probe->capacities[1])
    std::abort(); // This focused fixture never wraps its 1MiB CONTROL lane.
  const auto *base = reinterpret_cast<const unsigned char *>(probe) + probe->offsets[1];
  for (std::uint64_t offset = 0; offset < end;) {
    std::uint32_t length = 0, site = 0;
    std::memcpy(&length, base + offset, 4);
    std::memcpy(&site, base + offset + 4, 4);
    if (length < 16 || length > end - offset)
      std::abort();
    // Actual selected final-result phase: kind9, parent absent, owner present.
    if (site == 1320 && length >= 19 && base[offset + 16] == 9 &&
        base[offset + 17] == 0 && base[offset + 18] == 1) {
      result_phase_seen = true;
      return true;
    }
    offset += length;
  }
  return false;
}
using Rename = int (*)(const char *, const char *);
using Pwrite = ssize_t (*)(int, const void *, std::size_t, off_t);
inline Rename forward_rename = nullptr;
inline Pwrite forward_pwrite = nullptr;
inline void prepare(const char *actual_target, std::string_view expected,
                    const char *receipt_journal = nullptr) {
  if (!forward_rename)
    forward_rename = reinterpret_cast<Rename>(::dlsym(RTLD_NEXT, "rename"));
  if (!forward_rename)
    std::abort();
  live = {};
  target = actual_target;
  literal = expected;
  acquired_extent = refused_size = 0;
  receipt_descriptor = -1;
  receipt_next = receipt_short = 0;
  receipt_ready = receipt_error = false;
  allocation_mode = receipt_journal == nullptr;
  if (receipt_journal) {
    struct stat info{};
    if (::stat(receipt_journal, &info) != 0)
      std::abort();
    journal_device = info.st_dev;
    journal_inode = info.st_ino;
    if (!forward_pwrite)
      forward_pwrite = reinterpret_cast<Pwrite>(::dlsym(RTLD_NEXT, "pwrite"));
    if (!forward_pwrite)
      std::abort();
  }
  renamed = refused = watching = false;
  ready = true;
}
inline void stop() noexcept {
  ready = watching = false;
  receipt_ready = false;
  target = nullptr;
}
} // namespace operation_cut

extern "C" int rename(const char *from, const char *to) {
  using namespace operation_cut;
  if (!forward_rename)
    forward_rename = reinterpret_cast<Rename>(::dlsym(RTLD_NEXT, "rename"));
  if (!forward_rename)
    std::abort();
  const int result = forward_rename(from, to), saved = errno;
  if (rename_cancel >= 0 && result == 0 && target && std::strcmp(target, to) == 0) {
    notify(rename_cancel);
    rename_cancel = -1;
  }
  if (ready && result == 0 && std::strcmp(target, to) == 0) {
    ready = false;
    renamed = true;
    watching = allocation_mode;
    receipt_ready = !allocation_mode;
  }
  errno = saved;
  return result;
}
extern "C" ssize_t pwrite(int descriptor, const void *data, std::size_t bytes,
                          off_t offset) {
  using namespace operation_cut;
  if (!forward_pwrite)
    forward_pwrite = reinterpret_cast<Pwrite>(::dlsym(RTLD_NEXT, "pwrite"));
  if (!forward_pwrite)
    std::abort();
  const int entered = errno;
  if (open_cut && bytes >= 56 && std::memcmp(data, "ARJ1", 4) == 0) {
    struct stat info{};
    const auto *body = static_cast<const unsigned char *>(data);
    if (::fstat(descriptor, &info) == 0 && info.st_dev == journal_device &&
        info.st_ino == journal_inode && body[6] == 2 && body[7] == 0 &&
        body[24] == 24 && body[25] == 0 && body[26] == 0 && body[27] == 0 &&
        body[32] == 1 && body[33] == 0 && body[34] == 5 && body[35] == 0) {
      open_cut = false;
      open_refused = true;
      errno = EIO;
      return -1;
    }
  }
  errno = entered;
  if (diagnostic_cut && bytes >= 32 && std::memcmp(data, "ARJ1", 4) == 0) {
    const auto *body = static_cast<const unsigned char *>(data);
    if (body[6] == 1 && body[7] == 0) {
      append_loan_paused = blackbird::raw_region_prepaid_bank() == nullptr;
      if (++diagnostic_writes == diagnostic_cut) {
        diagnostic_failed = true;
        errno = EIO;
        return -1;
      }
    }
  }
  if (receipt_ready) {
    struct stat info{};
    const bool owned = ::fstat(descriptor, &info) == 0 &&
                       info.st_dev == journal_device && info.st_ino == journal_inode;
    errno = entered;
    if (owned && receipt_descriptor == descriptor && receipt_short > 0 &&
        offset == receipt_next) {
      receipt_ready = false;
      receipt_error = true;
      errno = EIO;
      return -1;
    }
    const auto *body = static_cast<const unsigned char *>(data);
    // Actual complete68-byte ARJ1 semantic receipt at the start of its batch.
    if (owned && receipt_descriptor < 0 && bytes >= 68 &&
        std::memcmp(body, "ARJ1", 4) == 0 && body[6] == 2 && body[7] == 0 &&
        body[24] == 36 && body[25] == 0 && body[26] == 0 && body[27] == 0 &&
        body[32] == 1 && body[33] == 0 && body[34] == 11 && body[35] == 0 &&
        body[36] == 0 && body[37] == 0 && body[38] == 0 && body[39] == 0 &&
        body[56] == 1 && body[57] == 0) {
      std::memcpy(actual_receipt.data(), data, actual_receipt.size());
      if (publication_notice >= 0) {
        notify(publication_notice);
        char released = 0;
        if (::read(publication_release, &released, 1) != 1)
          std::abort();
        publication_notice = -1;
      }
      const auto result = forward_pwrite(descriptor, data, 17, offset);
      if (result > 0 && result < static_cast<ssize_t>(bytes)) {
        receipt_descriptor = descriptor;
        receipt_short = result;
        receipt_next = offset + result;
      }
      return result;
    }
  }
  errno = entered;
  return forward_pwrite(descriptor, data, bytes, offset);
}
void *operator new(std::size_t bytes) {
  using namespace operation_cut;
  const int saved = errno;
  if (dispatch_copy_cut && dispatch_checkpoint_copy(bytes)) {
    dispatch_copy_cut = false;
    dispatch_copy_refused = true;
    errno = saved;
    throw std::bad_alloc{};
  }
  if (watching && (!require_result_phase || result_phase()))
    for (const auto &allocation : live)
      if (allocation.bytes >= literal.size() &&
          readable(allocation.pointer, literal.size()) &&
          std::memcmp(allocation.pointer, literal.data(), literal.size()) == 0) {
        acquired_extent = allocation.bytes;
        refused_size = bytes;
        watching = false;
        refused = true;
        errno = saved;
        throw std::bad_alloc{};
      }
  const auto extent = bytes == 0 ? std::size_t{1} : bytes;
  void *pointer = std::malloc(extent);
  if (!pointer)
    throw std::bad_alloc{};
  if (watching) {
    std::memset(pointer, 0, extent);
    auto *slot = static_cast<Allocation *>(nullptr);
    for (auto &allocation : live)
      if (!allocation.pointer) {
        slot = &allocation;
        break;
      }
    if (!slot)
      std::abort();
    *slot = {pointer, extent};
  }
  errno = saved;
  return pointer;
}
void operator delete(void *pointer) noexcept {
  using namespace operation_cut;
  if (watching)
    for (auto &allocation : live)
      if (allocation.pointer == pointer)
        allocation = {};
  std::free(pointer);
}
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}

extern "C" int clock_gettime(clockid_t method, timespec *value)
#if defined(__linux__)
    noexcept
#endif
{
  using Native = int (*)(clockid_t, timespec *);
  static const auto forward =
      reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "clock_gettime"));
  if (!forward)
    std::abort();
  int result;
  if (operation_cut::clock_watch && operation_cut::clock_failure &&
      method == CLOCK_THREAD_CPUTIME_ID) {
    operation_cut::clock_failure = false;
    errno = EIO;
    result = -1;
  } else
    result = forward(method, value);
  const int saved = errno;
  if (operation_cut::clock_watch) {
    if (operation_cut::clock_count == operation_cut::clocks.size())
      std::abort();
    operation_cut::clocks[operation_cut::clock_count++] = {
        method, result, saved, result == 0 ? *value : timespec{}};
  }
  errno = saved;
  return result;
}

extern "C" ssize_t sendmsg(int descriptor, const msghdr *message, int flags) {
  using Native = ssize_t (*)(int, const msghdr *, int);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "sendmsg"));
  if (!forward)
    std::abort();
  const int result_errno = errno;
  if (operation_cut::register_watch && message->msg_iovlen == 1 &&
      message->msg_iov[0].iov_len == 80 && message->msg_controllen != 0) {
    const auto *bytes =
        static_cast<const unsigned char *>(message->msg_iov[0].iov_base);
    std::memcpy(operation_cut::lane_epoch.data(), bytes, 16);
    std::memcpy(operation_cut::lane_capacity.data(), bytes + 16, 24);
    std::memcpy(operation_cut::lane_name.data(), bytes + 40, 32);
  }
  errno = result_errno;
  return forward(descriptor, message, flags);
}

extern "C" int poll(pollfd *descriptors, nfds_t count, int timeout) {
  using Native = int (*)(pollfd *, nfds_t, int);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "poll"));
  if (!forward)
    std::abort();
  const int saved = errno;
  bool cancellation = false, stream = false;
  for (nfds_t index = 0; index != count; ++index) {
    cancellation =
        cancellation || descriptors[index].fd == operation_cut::cancellation_watch;
    int type = 0;
    socklen_t length = sizeof(type);
    stream = stream || (descriptors[index].events == POLLIN &&
                        ::getsockopt(descriptors[index].fd, SOL_SOCKET, SO_TYPE, &type,
                                     &length) == 0 &&
                        type == SOCK_STREAM);
  }
  if (cancellation && stream && timeout != 0) {
    operation_cut::space_wait = true;
    if (operation_cut::space_cancel >= 0) {
      operation_cut::notify(operation_cut::space_cancel);
      operation_cut::space_cancel = -1;
    }
  }
  errno = saved;
  return forward(descriptors, count, timeout);
}
