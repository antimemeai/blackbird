#pragma once
#include "blackbird/admission.hpp"
#include "blackbird/colleague.hpp"
#include "blackbird/context.hpp"
#include "blackbird/participant_observation.hpp"
#include "blackbird/provider_original.hpp"
#include "blackbird/raw_participants.hpp"
#include <memory>
#include <vector>
namespace blackbird {
using ParticipantTransport =
    std::function<Value(const Value &, const ColleagueCapture &,
                        const std::function<bool()> &, const RawAdapterContext *)>;
// One owner calls controls/drain. Workers only touch their guarded state and
// bounded capture queue; admission is retained by the owner before launch/send.
// Ordinary controls refuse reentry from owner Retain/Save/Publish/cancelled
// callbacks. Mandatory shutdown remains callable from those callbacks.
class Participants {
public:
  using Retain = std::function<std::optional<SourceReference>(const OriginalCapture &)>;
  using Save = std::function<std::optional<ApplicationRecordId>(const Value &)>;
  using Publish = std::function<void(const Value &)>;
  using PublishNative = std::function<void(const Value &, RawEventRef)>;
  Participants(const Value &saved, Retain retain, Save save, Publish publish = {},
               PublishNative publish_native = {});
  ~Participants();
  Participants(const Participants &) = delete;
  Participants &operator=(const Participants &) = delete;
  Value configure(const Value &arguments);
  Value start(std::string id, const Value &arguments,
              const ParticipantTransport &transport, RawParticipantOrigin origin = {},
              DomainAdmission *admission = nullptr);
  Value read(const Value &arguments = Value::object({}));
  Value send(const Value &arguments, RawParticipantOrigin origin = {},
             DomainAdmission *admission = nullptr);
  Value cancel(const Value &arguments);
  Value await(const Value &arguments, const std::function<bool()> &cancelled = {});
  Value join(const Value &arguments, const std::function<bool()> &cancelled = {});
  Value archive(const Value &arguments);
  void drain();
  bool active() const;
  // Mandatory native cleanup uses only credits held before live publication.
  // Retention is a separate ordinary drain; it can remain pending after cancel.
  void shutdown();
  bool debt() const;
  ParticipantObservationReader observer() const;

private:
  friend class ParticipantObservationReader;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace blackbird
