#include "path_visual.hpp"
#include "planar_annotation_visual.hpp"
#include "roi_import_preview.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <imagejxx/composite_path.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
using namespace imagejxx::desktop;
namespace {
int checks = 0;
void check(bool value) {
  ++checks;
  if (!value) throw std::runtime_error("Path visual check " + std::to_string(checks));
}
template <class F> void refuses(F f) {
  bool caught = false;
  try { f(); } catch (const std::invalid_argument &) { caught = true; }
  check(caught);
}
} // namespace
int main() {
  try {
    auto path = Selection::from_path(std::vector<double>{
        0, 1, 1, 2, 4, -2, 7, 1, 3, 9, 3, 6, 7, 4, 6, 1, 1, 6, 4,
        0, 2, 3, 1, 4, 3, 1, 4, 5, 1, 2, 5, 4});
    path = with_roi_style(path, {{"stroke_color", std::string("#80112233")},
                                {"fill_color", std::string("#40224466")},
                                {"stroke_width", 2.5}});
    const auto original = path.path_commands();
    const auto small = pathVisual(path, 10, 8, {1, 1}, 8 * 1048576);
    const auto large = pathVisual(path, 10, 8, {100, 100}, 8 * 1048576);
    check(small.area && !small.markers && !small.edges.empty());
    check(large.edges.size() > small.edges.size());
    check(small.fill.size() == large.fill.size());
    check(path.path_commands() == original);
    check(small.style.stroke_width == 2.5);
    check(planarSelectionJson(path).at("path_version") == 1);
    check(planarSelectionJson(path).at("path").get<std::vector<double>>() == original);
    const auto controls = planarControlPoints(path);
    check(controls.size() == 11 && controls[1] == std::array<double, 2>{4, -2});
    const auto mask = path.mask(10, 8);
    std::vector<bool> covered(80);
    for (const auto &span : small.fill)
      for (int x = span.x; x < span.x + span.width; ++x)
        covered[std::size_t(span.y) * 10 + x] = true;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 10; ++x)
        if (covered[std::size_t(y) * 10 + x] != (mask.at(x, y) != 0))
          throw std::runtime_error("Path fill differs from explicit native mask");
    check(true);
    refuses([&] { pathVisual(path, 10, 8, {0, 1}, 1048576); });
    refuses([&] { pathVisual(path, 10, 8, {1, 1}, 1); });
    refuses([&] { pathVisual(Selection("rectangle", {{0, 0}, {1, 1}}), 10, 8, {1, 1}, 1048576); });
    auto line = Selection::from_path(std::vector<double>{0, -1e9, -1e9, 1, 1e9, 1e9});
    const auto distant = pathVisual(line, 10, 8, {1, 1}, 1048576);
    check(distant.edges.size() == 2 && distant.fill.empty());
    auto cleared = with_roi_style(path, {{"stroke_color", std::monostate{}},
                                      {"fill_color", std::monostate{}}});
    const auto empty = pathVisual(cleared, 10, 8, {1, 1}, 1048576);
    check(!empty.edges.empty() && empty.fill.empty());
    check(pathVisualBytes(large) > pathVisualBytes(small));
    auto tiny=Selection::from_path(std::vector<double>{0,2,2,3,2.1,2.1,1.9,2.1,2,2,4});
    const auto dot=pathVisual(tiny,10,8,{1,1},1048576);
    check(!dot.edges.empty() && dot.edges[0][0]==dot.edges[0][2] && dot.edges[0][1]==dot.edges[0][3]);
    auto distant_curve=Selection::from_path(std::vector<double>{0,1e8,1e8,3,1e8+10000,1e8-10000,1e8+20000,1e8+10000,1e8+30000,1e8});
    check(pathVisual(distant_curve,10,8,{100,100},1024).edges.empty());
    auto accuracy=[&](const std::vector<double> &commands,bool cubic) {
      auto p=Selection::from_path(commands);
      const auto visual=pathVisual(p,12,12,{8,32},1048576);
      for(int sample=0;sample<=1000;++sample) {
        const double t=sample/1000.,u=1-t;
        const double x=(cubic?30*t*u*u+10*t*t*t:10*t)*8;
        const double y=(cubic?30*t*t*u+10*t*t*t:20*t*u)*32;
        double nearest=std::numeric_limits<double>::infinity();
        for(const auto &edge:visual.edges) {
          const double ax=edge[0]*8,ay=edge[1]*32,dx=(edge[2]-edge[0])*8,dy=(edge[3]-edge[1])*32;
          const double q=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.,1.);
          nearest=std::min(nearest,std::hypot(x-ax-q*dx,y-ay-q*dy));
        }
        if(nearest>.251)throw std::runtime_error("Curve display deviation exceeds a quarter pixel");
      }
      check(true);
    };
    accuracy({0,0,0,2,5,10,10,0},false);
    accuracy({0,0,0,3,10,0,0,10,10,10},true);
    const DatasetShape huge{1,1,1,100000,100000,1};
    PlanarAnnotation stroke_only{"line",line,Plane::XY,0,{},{},{}};
    admit_planar_annotation_visuals(huge,{stroke_only},1048576);
    check(true);
    stroke_only.selection=with_roi_style(line,{{"fill_color",std::string("blue")}});
    refuses([&]{admit_planar_annotation_visuals(huge,{stroke_only},1048576);});
    const DatasetShape shape{1,1,1,8,10,1};
    imagejxx::Document owner(DocumentData{Dataset::from_planes(shape,{Image(10,8,1,PixelType::UInt8)})});
    ImageJRoi source;source.roi={"curve",path,{},{},0,{}};
    ImageJRoiPlaneMapping mapping;mapping.extent=shape;
    const auto mapped=map_imagej_roi(source,mapping);
    const auto bytes=encode_imagej_roi_native_envelope(mapped,source);
    auto imported=resolve_imagej_roi_import(owner.snapshot(),{{"curve.roi",bytes,{},{}}});
    RoiImportPreview preview(imported,8*1048576);
    check(preview.matches(owner.snapshot(),8*1048576));
    std::cout << "PASS " << checks << " path visual checks\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
