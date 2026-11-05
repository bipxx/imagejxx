#include <imagejxx/point_presentation.hpp>
#include <imagejxx/point_raster.hpp>
#include <limits>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/imagej_roi_projection.hpp>
#include <stdexcept>
#include <iostream>
using namespace imagejxx;
void check(bool value) { if(!value)throw std::runtime_error("point presentation check failed"); }
int refusal_cases=0;
template<class E,class F> void refuses(F f) { const auto index=++refusal_cases;try { f(); } catch(const E&) { return; } throw std::runtime_error("missing expected refusal "+std::to_string(index)); }
int main() try {
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
  auto relative=options;relative.relative_primitives=true;relative.round_centers=false;
  const auto templ=point_presentation(styled,relative);
  check(templ.glyphs[0].center==std::array<double,2>{2.5,3.5});
  check(templ.primitives[0].coordinates==std::array<double,4>{7,26,0,0});
  check(!templ.glyphs[0].visible(PlanePosition{0,0,0}) &&
      templ.glyphs[0].visible(PlanePosition{0,0,0},Plane::XZ) &&
      templ.glyphs[0].visible(PlanePosition{1,1,0},Plane::XY,true));
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
  Selection dot("point");dot.points={{4,4}};PointStyle raster_style;
  raster_style.marker=PointMarker::Cross;raster_style.size=0;raster_style.labels=false;
  dot=with_point_style(dot,raster_style);
  Image rgb(10,10,3,PixelType::UInt8);PointPresentationOptions raster_options;
  raster_options.color={200,80,40,128};
  const auto flattened=render_point_style(rgb,dot,raster_options);
  check(flattened.at(4,4,0)>0 && rgb.at(4,4,0)==0 && flattened.pixels!=rgb.pixels);
  PointRasterLimits raster_limits;raster_limits.max_working_bytes=1;
  auto bad_rgb=rgb;bad_rgb.pixels[0]=std::numeric_limits<double>::quiet_NaN();
  refuses<std::length_error>([&]{render_point_style(bad_rgb,dot,raster_options,nullptr,raster_limits);});
  raster_limits=PointRasterLimits{};raster_limits.max_pixel_samples=1;
  refuses<std::length_error>([&]{render_point_style(bad_rgb,dot,raster_options,nullptr,raster_limits);});
  refuses<std::invalid_argument>([&]{render_point_style(bad_rgb,dot,raster_options);});
  bad_rgb=rgb;bad_rgb.pixels.pop_back();
  refuses<std::invalid_argument>([&]{render_point_style(bad_rgb,dot,raster_options);});
  ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();
  refuses<OperationCancelled>([&]{render_point_style(rgb,dot,raster_options,nullptr,{},control);});
  control.cancellation=std::make_shared<CancellationToken>();int checks=0;
  control.checkpoint=[&]{if(++checks==4)control.cancellation->cancel();};
  refuses<OperationCancelled>([&]{render_point_style(rgb,dot,raster_options,nullptr,{},control);});
  check(rgb.at(4,4,0)==0);
  PointRasterFont font;font.font_size=9;
  for(auto& digit:font.digits){digit.width=1;digit.height=1;digit.offset_y=-1;digit.advance=2;digit.coverage={128};}
  auto labeled=with_point_style(selection,PointStyle{});
  const auto text_raster=render_point_style(Image(96,96,3,PixelType::UInt8),labeled,{},&font);
  check(text_raster.at(5,14,0)>0);
  refuses<std::invalid_argument>([&]{render_point_style(rgb,labeled);});
  // Explicit generic display context shares point pixels and resolves ROI ink.
  auto ink=with_roi_style(dot,{{"stroke_color",std::string("#80112233")}});
  auto ink_options=raster_options;ink_options.color={17,34,51,128};
  check(render_roi_style(rgb,ink,{1,2,3,255},raster_options).pixels==render_point_style(rgb,ink,ink_options).pixels);
  check(render_roi_style(rgb,ink).pixels==render_roi_style(rgb,without_point_style(ink)).pixels);
  check(render_roi_style(Image(96,96,3,PixelType::UInt8),labeled,{255,255,0,255},{},&font).pixels==text_raster.pixels);
  refuses<std::invalid_argument>([&]{render_roi_style(rgb,labeled,{255,255,0,255},{});});
  PointRasterLimits no_space;no_space.max_working_bytes=1;
  refuses<std::length_error>([&]{render_roi_style(bad_rgb,dot,{255,255,0,255},{},nullptr,no_space);});
  Selection legacy("rectangle",{{1,1},{5,5}});
  PointRasterFont irrelevant;irrelevant.font_size=-1;
  check(render_roi_style(rgb,legacy,{7,8,9,255},{},&irrelevant,no_space).pixels==render_roi_style(rgb,legacy,{7,8,9,255}).pixels);
  ExecutionControl cancelled;cancelled.cancellation=std::make_shared<CancellationToken>();cancelled.cancellation->cancel();
  refuses<OperationCancelled>([&]{render_roi_style(rgb,dot,{1,2,3,255},{},nullptr,{},cancelled);});
  refuses<OperationCancelled>([&]{render_roi_style(rgb,legacy,{1,2,3,255},{},nullptr,{},cancelled);});
  // Empty legacy point has no periodic pixel work: callback count is stable.
  Selection empty_legacy("point");
  ExecutionControl late;late.cancellation=std::make_shared<CancellationToken>();int callbacks=0;
  late.checkpoint=[&]{++callbacks;};
  (void)render_roi_style(rgb,empty_legacy,{1,2,3,255},{},nullptr,{},late);
  const int last=callbacks;callbacks=0;late.checkpoint=[&]{if(++callbacks==last)late.cancellation->cancel();};
  refuses<OperationCancelled>([&]{render_roi_style(rgb,empty_legacy,{1,2,3,255},{},nullptr,{},late);});
  font.digits[0].coverage.clear();
  refuses<std::invalid_argument>([&]{render_point_style(rgb,labeled,{},&font);});
  std::cout<<"Point presentation: bounded flat layout, semantic adapters and metadata preservation passed\n";
} catch(const std::exception& error) {
  std::cerr << error.what() << "\n";return 1;
}
