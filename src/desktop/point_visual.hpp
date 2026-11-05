#pragma once
#include "desktop.hpp"
#include <imagejxx/point_presentation.hpp>
#include <imagejxx/settings.hpp>
namespace imagejxx::desktop {
// Immutable glyph template owned by RoiVisual. No source/provider retention.
struct PointVisual {
  PointStyle style;
  std::string error;
  mutable bool show_all_override=false;
  mutable float gui_scale=1;
  PointPresentation layout;
  std::size_t cap=0;
  mutable std::size_t visible_count=0, emitted_primitives=0;
  mutable std::array<std::size_t,32> visible_indices{};
};
std::shared_ptr<PointVisual> pointVisual(const Selection&,RoiColor,std::size_t);
std::size_t pointVisualBytes(const RoiVisual&);
void drawPointVisual(const PointVisual&,ImDrawList*,std::array<float,2>,
    std::array<float,2>,std::optional<PlanePosition>,Plane);
Json pointVisualJson(const PointVisual&);
PointStyle pointCreationStyle(const Settings&);
RoiColor pointCreationColor(const Settings&);
Selection pointCreationPresentation(Selection,const PointStyle&,RoiColor);
struct PointStyleEdit {bool changed=false,clear=false;};
PointStyleEdit drawPointStyleControls(PointStyle&,bool&,std::string&,unsigned,
    const std::function<void(const char*,bool)>&);
}
