#pragma once
#include "blackbird/retained_state.hpp"
#include "blackbird/value.hpp"

namespace blackbird {
// File version 2 (version 1 is still read). The context goes through the shared
// validate_saved_context; a refused slot is ignored and reported through report,
// and only the validator's corrupt is a refusal (other errors fail the load).
// Derived current-state reduction, not a serialized Snapshot or physical catalog.
// Historical bytes remain authoritative and are deliberately absent here.
struct SavedState {
  JournalCursor boundary;
  std::uint64_t fact_count = 0;
  std::uint64_t issuer_counter = 0;
  Value context;
  Value programs;
  std::vector<RetainedFact> unresolved;
  unsigned slot = 0;
};
// What a load found beyond the selected slot: every slot that was present and
// refused (its slot and the reason; the validator's error for a context fault),
// and the other accepted slot when it is older than the selected one.
struct SavedStateLoad {
  std::vector<std::pair<unsigned, Error>> refusals;
  std::optional<SavedState> alternate;
  // Accepted slots whose archive catalogue does not open (filled by RetainedState):
  // not refusals; the slot may still be the one used.
  std::vector<std::pair<unsigned, Error>> catalogue_faults;
};
// File faults a slot is refused for: Error{corrupt, detail}. Context faults keep
// the validator's own detail (SavedContextFault, below 100).
enum class SavedFileFault : std::int64_t {
  extent = 101,
  read,
  checksum,
  header,
  prefix,
  beyond_journal,
  anchor,
  packets,
  parts,
  unresolved,
  trailing
};
Result<std::optional<SavedState>>
load_saved_state(JournalDirectory &directory, std::string_view name,
                 FramedJournal &journal, JournalCapacity capacity,
                 bool before_scan = false, const RawRetainedOrigin &raw_origin = {},
                 SavedStateLoad *report = nullptr);
// The body of a state file between its prefix and its CRC: the context (entries
// and sections reassembled from their streamed parts in file version 2) and the
// programs; end is where the unresolved events begin. A malformed body is
// Error{corrupt, SavedFileFault}; a packet decode that fails otherwise (allocation,
// capacity past the value limits, unsupported) keeps its own error. Pure: the loader,
// the writer and the fuzzer use the same code.
struct SavedPayload {
  Value context, programs;
  std::size_t end = 0;
};
Result<SavedPayload> decode_saved_payload(ByteView body, std::uint64_t file_version,
                                          std::uint64_t context_size,
                                          std::uint64_t program_size);
// The file version 2 body for a payload's context and programs (end unused); the
// core and programs sizes go into the prefix.
struct EncodedSavedPayload {
  std::vector<std::byte> bytes;
  std::uint64_t context_size = 0, program_size = 0;
};
Result<EncodedSavedPayload> encode_saved_payload(const SavedPayload &payload);
// The same body appended straight to out (the writer's one file buffer), counted
// first so out grows once, with room reserved for trailing more bytes; on failure
// out is left as it was.
struct SavedPayloadSizes {
  std::uint64_t context_size = 0, program_size = 0;
};
Result<SavedPayloadSizes> append_saved_payload(std::vector<std::byte> &out,
                                               const SavedPayload &payload,
                                               std::size_t trailing = 0);
Result<void> publish_saved_state(JournalDirectory &directory, std::string_view name,
                                 FramedJournal &journal, const SavedState &saved,
                                 unsigned slot,
                                 const RawRetainedOrigin &raw_origin = {});
} // namespace blackbird
