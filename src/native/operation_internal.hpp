#pragma once
#include <imagejxx/operations.hpp>

namespace imagejxx {
struct ExecutionOptions;
class ResolvedTarget;
OperationParameters validate_execution_parameters(
    const std::string &operation, const ResolvedTarget &target,
    const OperationParameters &parameters, ExecutionOptions options);
// Additional admission estimate for parameter-dependent buffers and frozen
// numeric/image arguments, beyond the executor's image-sized base estimate.
// Parameters have already been normalized for this metadata. This does not
// count documents/history/caches or enforce an allocation arena.
std::size_t operation_extra_workspace(const std::string &operation,
                                      const OperationDefaultContext &context,
                                      const OperationParameters &parameters);
// Internal executor entry point: parameters have already been normalized for
// this exact plane metadata. The kernel still validates its pixel constraints.
OperationResult
execute_normalized_operation(const std::string &id, const Image &image,
                             const OperationParameters &parameters);
} // namespace imagejxx
