#pragma once
#include "desktop.hpp"
#include <imgui.h>
#include <memory>
namespace imagejxx {class ResolvedPlanarAnnotationCommand;}

namespace imagejxx::desktop {
using PlanarAnnotationPublisher=std::function<void(const ResolvedPlanarAnnotationCommand &)>;
struct PlanarCanvas {
  Plane plane;
  int coordinate, width, height;
  std::uint64_t id;
  std::array<float,2> origin,scale,extent;
  std::array<float,2> viewport_low{}, viewport_high{};
  float gui_scale = 1;
  bool operator==(const PlanarCanvas &) const;
};
// One creator-thread lane shared by all three canvases. Nothing publishes until
// a delivered release. Native geometry and transactions own all image logic.
class PlanarAnnotationPanel {
public:
  PlanarAnnotationPanel();
  ~PlanarAnnotationPanel();
  PlanarAnnotationPanel(PlanarAnnotationPanel &&) noexcept;
  PlanarAnnotationPanel &operator=(PlanarAnnotationPanel &&) noexcept;
  PlanarAnnotationPanel(const PlanarAnnotationPanel &)=delete;
  PlanarAnnotationPanel &operator=(const PlanarAnnotationPanel &)=delete;
  void controls(Document &,bool enabled,std::size_t max_working_bytes=256*1048576,const PlanarAnnotationPublisher &publish={});
  bool pointer(Document &,const PlanarCanvas &,bool enabled,bool hovered,const PlanarAnnotationPublisher &publish={});
  void draw(Document &,const PlanarCanvas &,ImDrawList *,bool show,bool editable=true);
  // Read-only canvas lane without drawing editor controls.
  void begin_draw(Document &,std::size_t max_working_bytes);
  void cancel(const char *reason);
  bool active() const;
  Json diagnostics() const;
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
