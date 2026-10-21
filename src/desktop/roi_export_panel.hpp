#pragma once
#include "roi_import_panel.hpp"
#include <imagejxx/imagej_roi_export.hpp>
#include <imagejxx/imagej_roi_zip_export.hpp>
#include <imagejxx/imagej_annotation_export.hpp>
namespace imagejxx::desktop {
class RoiExportPanel {
public:
  explicit RoiExportPanel(std::shared_ptr<JobScheduler>);
  ~RoiExportPanel();
  void show(const DocumentSnapshot &, MetadataObject original = {},
            bool zip = false);
  bool zip_mode() const;
  void cancel(const char *reason);
  bool visible() const;
  void poll();
  void wait() const;
  bool wait_for(std::chrono::milliseconds) const;
  std::shared_ptr<const RoiImportPreview> preview(const DocumentSnapshot &,
                                                  std::size_t) const;
  void preview_error(const std::string &);
  RoiImportBrowseRequest browse_request() const;
  bool browse_current(const RoiImportBrowseRequest &,
                      const std::optional<DocumentSnapshot> &) const;
  void path(const RoiImportBrowseRequest &,
            const std::optional<DocumentSnapshot> &, std::string);
  void
  draw(const std::optional<DocumentSnapshot> &, bool busy, std::size_t cap,
       const std::function<void()> &browse,
       const std::function<void(const PlanarAnnotation &)> &go_to,
       const std::function<void(const ResolvedImageJRoiExport &,
                                const std::function<void()> &)> &publish,
       const std::function<void(const ResolvedImageJRoiZipExport &,
                                const std::function<void()> &)> &publish_zip,
       const std::function<void(const PlanarDisplayAnnotation &)> &go_to_display,
       const std::function<void(const ResolvedImageJAnnotationExport &,
                                const std::function<void()> &)> &publish_mixed);
  Json diagnostics() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
