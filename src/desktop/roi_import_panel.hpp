#pragma once
#include "desktop.hpp"
#include <imagejxx/imagej_annotation_import.hpp>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/jobs.hpp>
namespace imagejxx {
struct NativeWorkflowImport;
}
namespace imagejxx::desktop {
class RoiImportPreview;
struct RoiImportBrowseRequest {
  DocumentSnapshot source;
  std::uint64_t generation;
  std::string path;
  std::uint64_t route = 0;
};
class RoiImportPanel {
public:
  explicit RoiImportPanel(std::shared_ptr<JobScheduler>);
  ~RoiImportPanel();
  void show(const DocumentSnapshot &);
  void show_record(const DocumentSnapshot &, MetadataObject);
  // Return typed choices through draw callbacks; no live document publication.
  void show_draft(const DocumentSnapshot &, MetadataObject);
  void show_draft(const DocumentSnapshot &,
                  std::shared_ptr<const NativeWorkflowImport>);
  RoiImportBrowseRequest browse_request() const;
  bool browse_current(const RoiImportBrowseRequest &,
                      const std::optional<DocumentSnapshot> &) const;
  void path(const RoiImportBrowseRequest &,
            const std::optional<DocumentSnapshot> &, std::string);
  void cancel(const char *reason);
  bool visible() const;
  bool working() const;
  std::optional<JobInfo> job_info() const;
  void poll();
  void wait() const;
  bool wait_for(std::chrono::milliseconds) const;
  std::shared_ptr<const RoiImportPreview> preview(const DocumentSnapshot &,
                                                  std::size_t cap) const;
  void preview_error(const std::string &);
  void draw(const std::optional<DocumentSnapshot> &, bool busy,
            std::size_t visual_cap, const std::function<void()> &browse,
            const std::function<void(Plane, int, std::optional<int>,
                                     std::optional<int>)> &navigate,
            const std::function<void(const ResolvedImageJRoiImport &)> &publish,
            const std::function<void(const ResolvedImageJAnnotationImport &)>
                &publish_mixed);
  Json diagnostics() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
