#pragma once
#include "desktop.hpp"
#include <imagejxx/planar_display_annotation_command.hpp>
namespace imagejxx::desktop {
class DisplayAnnotationPanel {
public:
  DisplayAnnotationPanel();
  ~DisplayAnnotationPanel();
  void show(const DocumentSnapshot &);
  void show_draft(const DocumentSnapshot &,std::size_t initial_row);
  std::optional<PlanarDisplayAnnotation> take_draft();
  void cancel(const char *reason);
  bool visible() const;
  void
  draw(Document *, bool busy, std::size_t cap,
       const std::function<void(const ResolvedPlanarDisplayAnnotationCommand &)>
           &publish,
       const std::function<Json()> &capture_metadata = {});
  Json diagnostics() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
