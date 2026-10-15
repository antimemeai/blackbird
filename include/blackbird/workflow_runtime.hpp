#pragma once
#include "blackbird/raw_runtime.hpp"
#include "blackbird/workflow_executor.hpp"
#include "blackbird/workflow_store.hpp"

namespace blackbird {
// Session-owner facade. All requests/ticks stay on its construction thread.
// No worker accesses the ledger or another task's context. The caller owns the
// session lease, raw runtime and AuditLog until after shutdown and result drain.
class WorkflowRuntime {
public:
  WorkflowRuntime(AuditLog &log, HandleDomain domain, WorkflowExecutorLimits limits,
                  RawRuntime *capture, WorkflowExecutor::Notify notify = {});
  ~WorkflowRuntime();
  WorkflowRuntime(const WorkflowRuntime &) = delete;
  WorkflowRuntime &operator=(const WorkflowRuntime &) = delete;
  Value request(const Value &input);
  void tick();
  void shutdown();
  bool stopping() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace blackbird
