#pragma once
#include "roi_import_panel.hpp"
#include <imagejxx/native_workflow.hpp>
#include <imagejxx/native_workflow_batch.hpp>
#include <imagejxx/native_workflow_program.hpp>
namespace imagejxx::desktop {
NativeWorkflowProgramLimits desktop_workflow_program_limits(std::size_t cap);
bool needs_native_workflow_review(const Json &);
bool scientific_operation_review_available(const std::string &);
bool scientific_binary_operation(const std::string &);
void admit_scientific_conversion_review(const DocumentSnapshot &, std::size_t);
bool scientific_operation_review_available(const std::string &,
                                           const DocumentSnapshot &);
// Admit legacy batch schemas/parameters before creating directories or jobs.
void validate_legacy_batch_workflow(const Json &);
class NativeWorkflowPanel {
public:
  explicit NativeWorkflowPanel(std::shared_ptr<JobScheduler>);
  ~NativeWorkflowPanel();
  void show(const DocumentSnapshot &, const Json &, std::size_t, bool recording,
            bool batch = false);
  // Seed one eligible scientific processing operation without changing the
  // owner's recorder table. The existing review owns targets/preview/Apply.
  void show_operation(const DocumentSnapshot &, const std::string &,
                      const Json &parameters, const Json &recorder,
                      std::size_t cap, bool recording);
  void seed_batch(const Json &, const std::string &);
  void show_input_draft(const DocumentSnapshot &, const Json &, std::size_t,
                        const std::string &);
  void cancel(const char *);
  void poll();
  void release_textures();
  void wait() const;
  bool wait_for(std::chrono::milliseconds) const;
  bool working() const;
  std::optional<JobInfo> job_info() const;
  void request_cancel() const;
  bool visible() const;
  RoiImportBrowseRequest browse_request() const;
  bool browse_current(const RoiImportBrowseRequest &,
                      const std::optional<DocumentSnapshot> &) const;
  void path(const RoiImportBrowseRequest &,
            const std::optional<DocumentSnapshot> &, std::string,
            bool replace_existing = false);
  void
  draw(const std::optional<DocumentSnapshot> &, bool, std::size_t, bool,
       const Json &, const std::function<void()> &browse,
       const std::function<void(const PreparedNativeWorkflow &,
                                const std::function<void()> &)> &publish,
       const std::function<void(const DocumentSnapshot &,
                                const std::function<void()> &)> &publish_batch,
       const std::function<void(const DocumentSnapshot &, Json)>
           &publish_program,
       bool gl = false, bool diagnostics = false);
  Json diagnostics() const;
  Json recorded_operation() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
