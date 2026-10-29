#pragma once
#include <imagejxx/operations.hpp>
namespace imagejxx { class ResolvedTarget; }
namespace imagejxx::detail {
// Shared scheduler/command admission base for already normalized plane operations.
// Target masks are separately bounded by the caller's remaining allowance.
std::size_t plane_operation_base_resources(const std::string&,const ResolvedTarget&,
                                           const OperationParameters&);
}
