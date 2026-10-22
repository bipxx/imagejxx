#pragma once
#include <imagejxx/rgb_contrast_apply_command.hpp>
#include <imagejxx/contrast_apply_job.hpp>
namespace imagejxx {
// RGB and scalar jobs share cancellation, retained leases and atomic publication.
ContrastApplyJobEstimate estimate_rgb_contrast_apply_job(const ResolvedRgbContrastApplyCommand &,
    const ContrastApplyCommandLimits & = {},const JobOptions & = {});
inline ContrastApplyJob submit_rgb_contrast_apply(JobScheduler &scheduler,
    const ResolvedRgbContrastApplyCommand &command,const ContrastApplyCommandLimits &limits = {},JobOptions options = {}){
  return ContrastApplyJob::submit(scheduler,command,limits,std::move(options));
}
} // namespace imagejxx
