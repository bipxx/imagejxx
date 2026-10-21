#pragma once
#include "planar_annotation_panel.hpp"
#include <imagejxx/planar_display_annotations.hpp>
namespace imagejxx::desktop {
// Logical retained/draw storage estimate; no source image sampling.
void admit_display_annotation_visuals(
    const std::vector<PlanarDisplayAnnotation> &, std::size_t cap);
class DisplayAnnotationRenderer {
public:
  DisplayAnnotationRenderer();
  ~DisplayAnnotationRenderer();
  DisplayAnnotationRenderer(DisplayAnnotationRenderer &&) noexcept;
  DisplayAnnotationRenderer &operator=(DisplayAnnotationRenderer &&) noexcept;
  Json draw(const DocumentSnapshot &, const PlanarCanvas &, ImDrawList *,
            bool gl, bool show, std::size_t cap, bool diagnostics);
  // Creator-thread call while the owning GL context is still alive.
  void release_textures();
  void clear();

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
