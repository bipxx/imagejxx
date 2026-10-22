#pragma once
#include <imagejxx/contrast_apply.hpp>
namespace imagejxx::detail {
void validate_contrast_apply_options(PixelType,int,const ContrastApplyOptions &);
std::size_t contrast_apply_working_bytes(const ResolvedTarget &,
    const ContrastApplyOptions &,const ContrastApplyLimits &);
}
