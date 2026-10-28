#include <imagejxx/application.hpp>
#include <limits>
#include "target_calibration_internal.hpp"

namespace imagejxx {
std::shared_ptr<const GlobalCalibration> Application::global_calibration() const {
  std::lock_guard<std::mutex> lock(mutex_);return global_calibration_;
}
std::uint64_t Application::global_calibration_generation() const {
  std::lock_guard<std::mutex> lock(mutex_);return global_calibration_generation_;
}
void Application::set_global_calibration(const GlobalCalibration& value) {
  auto next=std::make_shared<const GlobalCalibration>(value);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(global_calibration_generation_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Global calibration generation exhausted");
    global_calibration_.swap(next);++global_calibration_generation_;
  } // Retire the previous immutable value after releasing membership lock.
}
void Application::clear_global_calibration() {
  std::shared_ptr<const GlobalCalibration> retired;
  {
    std::lock_guard<std::mutex> lock(mutex_);if(!global_calibration_)return;
    if(global_calibration_generation_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Global calibration generation exhausted");
    global_calibration_.swap(retired);++global_calibration_generation_;
  }
}
GlobalCalibration Application::local_calibration(std::uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);return GlobalCalibration::from_snapshot(lookup_locked(id)->snapshot());
}
bool Application::ignores_global_calibration(std::uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);const auto owner=lookup_locked(id);
  const auto found=calibration_bypass_.find(owner->id);return found!=calibration_bypass_.end() && found->second.ignore;
}
void Application::set_ignore_global_calibration(bool ignore, std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);const auto owner=lookup_locked(id);
  auto found=calibration_bypass_.find(owner->id);
  if(found==calibration_bypass_.end()) {
    if(ignore)calibration_bypass_.emplace(owner->id,CalibrationBypass{true,1});
    return;
  }
  if(found->second.ignore==ignore)return;
  if(found->second.generation==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Calibration bypass generation exhausted");
  found->second.ignore=ignore;++found->second.generation;
}
EffectiveCalibration Application::effective_calibration(std::uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);return capture_calibration_locked(lookup_locked(id)->snapshot());
}
EffectiveCalibration Application::capture_calibration_locked(const DocumentSnapshot& source) const {
  const auto bypass=calibration_bypass_.find(source.id());
  const bool ignore=bypass!=calibration_bypass_.end() && bypass->second.ignore;
  auto result=resolve_effective_calibration(source,global_calibration_.get(),ignore);
  result.global_generation_=global_calibration_generation_;
  result.bypass_generation_=bypass==calibration_bypass_.end()?0:bypass->second.generation;
  result.application_=job_identity_;return result;
}
void Application::validate_effective_calibration(const EffectiveCalibration& captured) const {
  std::lock_guard<std::mutex> lock(mutex_);
  detail::TargetCalibrationBinding binding;
  binding.application=captured.application_;binding.global_generation=captured.global_generation_;
  binding.bypass_generation=captured.bypass_generation_;binding.ignore=captured.ignores_global_;
  validate_calibration_binding_locked(binding,captured.document_id_);
  const auto source=lookup_locked(captured.document_id_)->snapshot();
  if(!captured.source_revisions_.matches(source.revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
}
void Application::validate_calibration_binding_locked(const detail::TargetCalibrationBinding& binding,std::uint64_t id) const {
  if(binding.application.lock()!=job_identity_)throw std::invalid_argument("Calibration capture belongs to another application or is detached");
  (void)lookup_locked(id);
  const auto bypass=calibration_bypass_.find(id);
  const auto generation=bypass==calibration_bypass_.end()?0:bypass->second.generation;
  if(generation!=binding.bypass_generation || (!binding.ignore && binding.global_generation!=global_calibration_generation_))throw RevisionConflict();
}
void Application::validate_target_calibration_locked(const ResolvedTarget& target) const {
  if(target.calibration_binding_)validate_calibration_binding_locked(*target.calibration_binding_,target.document_id());
}
} // namespace imagejxx
