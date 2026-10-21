#include "shadow_demo.hpp"
#include <array>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
const std::array<const char *,8> directions{"north","northeast","east","southeast","south","southwest","west","northwest"};
}
ShadowDemo::ShadowDemo(imagejxx::Application &application)
    : application_(application), scheduler_(application.job_scheduler()) {}
ShadowDemo::~ShadowDemo() { release(); }
bool ShadowDemo::eligible(const DocumentSnapshot &source) {
  const auto &data=source.data().dataset;const auto shape=data.shape();const auto type=data.pixel_type();
  return shape.plane_count()==1 && data.sample_policy()==FloatingSamplePolicy::FiniteOnly &&
      ((type==PixelType::UInt8 && (shape.s==1 || shape.s==3)) ||
       ((type==PixelType::UInt16 || type==PixelType::Float32) && shape.s==1));
}
void ShadowDemo::release() {
  preview_.reset();
  if(job_)job_->cancel();
  job_.reset();job_id_.store(0);
}
void ShadowDemo::enqueue() {
  ExecutionOptions execution;execution.phase=ExecutionPhase::Preview;
  JobOptions options;options.label="Shadows Demo";options.retain_result_resources=true;
  options.supersedes="desktop.shadow_demo";
  job_=application_.submit("shadow_imagej",*target_,{{"direction",std::string(directions[shown_%8])}},execution,options);
  job_id_.store(job_->info().id);displayed_=false;
}
void ShadowDemo::start(const DocumentSnapshot &source) {
  if(active_)throw std::invalid_argument("Shadows Demo is already running");
  if(!eligible(source))throw std::invalid_argument("Shadows Demo requires one finite ImageJ gray or RGB image");
  auto target=resolve_target(source,{},"shadow_imagej");
  release();source_=source;target_=std::move(target);owner_=source.id();shown_=0;
  error_.clear();reason_="running";canceled_.store(false);active_=true;
  try {enqueue();}catch(const std::exception &e){error_=e.what();cancel("error");throw;}
}
void ShadowDemo::request_cancel() noexcept {
  canceled_.store(true);
  try {if(const auto id=job_id_.load())scheduler_->cancel(id);}catch(...){}
}
void ShadowDemo::cancel(const char *reason) {
  active_=false;reason_=reason;release();target_.reset();source_.reset();
}
void ShadowDemo::wait() const { if(job_)job_->wait(); }
void ShadowDemo::poll(const std::optional<DocumentSnapshot> &current,std::size_t cap) {
  if(!active_)return;
  if(canceled_.load()){cancel("canceled");return;}
  if(!current || !source_->same_document(*current) ||
      !source_->revisions().matches(current->revisions(),Change::Data)) {cancel("source_changed");return;}
  if(job_ && job_->info().resource_bytes>cap) {
    error_="Shadows Demo exceeds the current memory limit";cancel("memory_limit");return;
  }
  try {
    const auto now=std::chrono::steady_clock::now();
    if(displayed_) {
      if(now<due_)return;
      release();
      if(shown_==160){cancel("completed");return;}
      enqueue();
    }
    if(job_ && job_->ready()) {
      auto prepared=job_->prepared();preview_=prepared.preview();++shown_;displayed_=true;
      due_=std::chrono::steady_clock::now()+std::chrono::milliseconds(50);
    }
  }catch(const OperationCancelled &){cancel("canceled");}
   catch(const std::exception &e){error_=e.what();cancel("error");}
}
std::optional<DocumentSnapshot> ShadowDemo::preview(const DocumentSnapshot &current) const {
  if(!active_ || !preview_ || !source_ || !source_->same_document(current) ||
      !source_->revisions().matches(current.revisions(),Change::Data))return {};
  return preview_;
}
Json ShadowDemo::diagnostics() const {
  return {{"active",active_},{"shown",shown_},{"total",160},{"cycles",20},{"interval_ms",50},
      {"direction",shown_?Json(directions[(shown_-1)%8]):Json()}, {"generation",shown_},
      {"source_id",owner_},{"reason",reason_},{"error",error_},{"retained_preview",preview_.has_value()},
      {"job_id",job_id_.load()},{"resource_bytes",job_?job_->info().resource_bytes:0}};
}
}
