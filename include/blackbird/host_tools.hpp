#pragma once
#include "blackbird/value.hpp"
#include <functional>

namespace blackbird {
// A trusted application's native, connection-local operation catalog.
// Its schemas use owned kind/fields/element values, never a JSON IPC contract.
class HostToolCatalog {
public:
  explicit HostToolCatalog(Value catalog);
  const Value &native() const noexcept { return catalog_; }
  const Value *find(std::string_view name) const;
  Value names() const;
  // Convert to provider schemas only when assembling a provider request.
  Value provider_tools() const;
  void validate_arguments(std::string_view name, const Value &arguments) const;
  void validate_result(std::string_view name, const Value &result) const;

private:
  Value catalog_;
};
struct HostToolCall {
  std::string_view name;
  const Value &arguments;
  OperationAttemptId attempt;
  InvocationId invocation;
  DefinitionGenerationId generation;
};
using HostToolCallback = std::function<Value(const HostToolCall &)>;
} // namespace blackbird
