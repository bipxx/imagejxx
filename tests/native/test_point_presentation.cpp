#include <imagejxx/point_presentation.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/imagej_roi_projection.hpp>
#include <stdexcept>
#include <iostream>
using namespace imagejxx;
void check(bool value) { if(!value)throw std::runtime_error("point presentation check failed"); }
template<class E,class F> void refuses(F f) { try { f(); } catch(const E&) { return; } throw std::runtime_error("missing expected refusal"); }
int main() {
  Selection selection("multipoint");selection.points={{2.5,3.5},{7,9}};selection.width=17;
  selection.attributes["opaque"]=std::string("preserve");
  auto tagged=with_point_annotations(selection,{{0,PlanePosition{0,0,1}},{2,{}}});
  PointStyle style;style.marker=PointMarker::Circle;style.size=6;
  auto styled=with_point_style(tagged,style);
  check(styled.width==17 && styled.points==selection.points && point_annotations(styled)[1].counter==2);
  check(point_style(styled)->size==6 && without_point_style(styled)==tagged);
  PointPresentationOptions options;options.position=PlanePosition{0,0,0};
  auto layout=point_presentation(styled,options);
  check(layout.glyphs.size()==1 && layout.glyphs[0].index==1);
  check(layout.primitives.size()==2 && layout.primitives[0].text=="2" && layout.primitives[0].font_size==20);
  check(layout.primitives[1].stroke_width==2 && layout.primitives[1].color==point_counter_color(2));
  options.plane=Plane::XZ;check(point_presentation(styled,options).glyphs.size()==2);
  options.plane=Plane::XY;options.show_all=true;layout=point_presentation(styled,options);
  check(layout.glyphs.size()==2 && layout.glyphs[0].center==std::array<double,2>{3,4});
  PointPresentationLimits cap;cap.max_working_bytes=layout.working_bytes-1;
  refuses<std::length_error>([&]{point_presentation(styled,options,cap);});
  cap.max_working_bytes=layout.working_bytes;check(point_presentation(styled,options,cap).glyphs.size()==2);
  options.clip=std::array<double,4>{1000,1000,1001,1001};cap.max_primitives=0;
  check(point_presentation(styled,options,cap).glyphs.empty());
  options.clip.reset();refuses<std::length_error>([&]{point_presentation(styled,options,cap);});
  ImageJRoi wire;wire.roi.selection=selection;wire.options=8192|8;wire.properties="original";
  wire=with_imagej_point_style(wire,style);
  check(wire.options==(8192|8|1024) && wire.point_type==3 && wire.point_size==6 && wire.properties=="original");
  check(imagej_point_style(wire).size==6);
  wire.point_type=255;refuses<std::invalid_argument>([&]{imagej_point_style(wire);});
  PlanarAnnotation row{"styled",styled,Plane::XY,0,{},{},{}};
  auto projected=imagej_roi_annotation_projection(row,{1,1,2,20,20,1});
  check(projected.point_type==3 && projected.point_size==6 && (projected.options&1024));
  check(!projected.roi.selection.attributes.count(point_style_attribute));
  styled.attributes[point_style_attribute]=metadata_object({{"version",std::int64_t(2)}});
  refuses<std::invalid_argument>([&]{with_point_style(styled,style);});
  check(without_point_style(styled)==tagged);
  std::cout<<"Point presentation: bounded flat layout, semantic adapters and metadata preservation passed\n";
}
