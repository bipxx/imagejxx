#pragma once
#include "roi_import_panel.hpp"
#include <imagejxx/imagej_annotation_export.hpp>
namespace imagejxx {
struct NativeWorkflowExport;
}
namespace imagejxx::desktop {
class RoiMixedExportPanel {
public:
  explicit RoiMixedExportPanel(std::shared_ptr<JobScheduler>);
  ~RoiMixedExportPanel();
  void show(const DocumentSnapshot &, MetadataObject, bool zip);
  // Choice-only review; draw accepts the path-free resolved equation.
  void show_draft(const DocumentSnapshot &, MetadataObject, bool zip);
  void show_draft(const DocumentSnapshot &,
                  std::shared_ptr<const NativeWorkflowExport>);
  void cancel(const char *);
  void poll();
  bool working() const;
  std::optional<JobInfo> job_info() const;
  bool visible() const;
  bool zip_mode() const;
  void wait() const;
  bool wait_for(std::chrono::milliseconds) const;
  RoiImportBrowseRequest browse_request() const;
  bool browse_current(const RoiImportBrowseRequest &,
                      const std::optional<DocumentSnapshot> &) const;
  void path(const RoiImportBrowseRequest &,
            const std::optional<DocumentSnapshot> &, std::string);
  std::shared_ptr<const RoiImportPreview> preview(const DocumentSnapshot &,
                                                  std::size_t) const;
  void preview_error(const std::string &);
  void draw(const std::optional<DocumentSnapshot> &, bool, std::size_t,
            const std::function<void()> &,
            const std::function<void(const PlanarAnnotation &)> &,
            const std::function<void(const PlanarDisplayAnnotation &)> &,
            const std::function<void(const ResolvedImageJAnnotationExport &,
                                     const std::function<void()> &)> &,
            const std::function<void(const ResolvedImageJAnnotationExport &)>
                &accept_choices = {});
  Json diagnostics() const;

private:
  void show_impl(const DocumentSnapshot &, MetadataObject, bool,
                 std::shared_ptr<const NativeWorkflowExport>);
  struct State;
  std::unique_ptr<State> state_;
};
bool needs_mixed_roi_export(const DocumentSnapshot &, const MetadataObject &);
} // namespace imagejxx::desktop
