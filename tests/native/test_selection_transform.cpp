#include <imagejxx/selection_transform.hpp>
#include <imagejxx/point_annotations.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void refuses(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Expected hull refusal");}
int main(){try{
  Selection input("multipoint",{{2,2},{12,2},{8,8},{12,12},{2,12},{2,2}});
  const std::vector<std::array<double,2>> expected{{2,2},{2,12},{12,12},{12,2}};
  check(convex_hull_selection(input,16,16).points==expected,"Hull ordering/concavity/duplicates differ");
  auto limits=SelectionEditLimits{};limits.max_working_bytes=estimate_convex_hull_selection(input,16,16);
  check(convex_hull_selection(input,16,16,false,limits).points==expected,"Exact hull cap refused");
  --limits.max_working_bytes;refuses([&]{convex_hull_selection(input,16,16,false,limits);});
  auto annotated=with_point_annotations(input,std::vector<PointAnnotation>(input.points.size(),PointAnnotation{2,{}}));
  refuses([&]{convex_hull_selection(annotated,16,16);});
  check(convex_hull_selection(annotated,16,16,true).attributes.count(point_annotations_attribute)==0,"Hull retained incompatible point annotations");
  refuses([&]{convex_hull_selection(Selection("line",{{0,0},{4,4}}),16,16);});
  refuses([&]{convex_hull_selection(Selection("multipoint",{{0,0},{1,1},{2,2}}),16,16);});
  ExecutionControl control;control.checkpoint=[&]{input.points={{1,1},{3,1},{2,3}};};
  const Selection original("multipoint",{{2,2},{12,2},{8,8},{12,12},{2,12}});input=original;
  check(convex_hull_selection(input,16,16,false,{},control).points==expected,"Callback redirected captured hull input");
  control.checkpoint=[] {throw std::runtime_error("cancel");};
  refuses([&]{convex_hull_selection(original,16,16,false,{},control);});
  const std::vector<std::array<double,2>> rectangle{{2,2},{13,13}};
  check(bounding_rectangle_selection(original,16,16).points==rectangle,"Point bounds must include endpoint pixels");
  limits=SelectionEditLimits{};limits.max_working_bytes=estimate_bounding_rectangle_selection(original,16,16);
  check(bounding_rectangle_selection(original,16,16,false,limits).points==rectangle,"Exact bounds cap refused");
  --limits.max_working_bytes;refuses([&]{bounding_rectangle_selection(original,16,16,false,limits);});
  refuses([&]{bounding_rectangle_selection(annotated,16,16);});
  check(bounding_rectangle_selection(annotated,16,16,true).attributes.count(point_annotations_attribute)==0,"Bounds retained incompatible point data");
  input=original;control.checkpoint=[&]{input.points={{0,0}};};
  check(bounding_rectangle_selection(input,16,16,false,{},control).points==rectangle,"Bounds callback redirected captured source");
  const auto region=Selection::from_packed_region(3,2,{0x44},{-5,7});
  check(bounding_rectangle_selection(region,16,16).points==std::vector<std::array<double,2>>{{-4,7},{-2,9}},"Region bounds lost occupied cells/off-canvas origin");
  refuses([&]{bounding_rectangle_selection(Selection::from_packed_region(3,2,{0},{-5,7}),16,16);});
  auto path=Selection::from_path(std::vector<double>{0,2,3,1,8,3,1,8,9,4});
  check(bounding_rectangle_selection(path,16,16).points==std::vector<std::array<double,2>>{{2,3},{8,9}},"Analytic path bounds differ");
  check(bounding_rectangle_selection(Selection("line",{{2,3},{12,3}}),16,16).points==std::vector<std::array<double,2>>{{2,3},{13,4}},"Horizontal line pixel bounds differ");
  std::cout<<"PASS Bounding Rectangle geometry, region/path/line bounds, metadata, caps and frozen callbacks\n";
  Selection circlePoints("multipoint",{{3,8},{8,3},{13,8},{8,13}});
  const std::vector<std::array<double,2>> circleBounds{{3,3},{13,13}};
  check(fit_circle_selection(circlePoints,16,16).points==circleBounds,"Exact point circle differs");
  Selection area("rectangle",{{3,3},{13,13}});
  check(fit_circle_selection(area,16,16).points==std::vector<std::array<double,2>>{{2,2},{13,13}},"Equal-area circle differs");
  limits={};limits.max_working_bytes=estimate_fit_circle_selection(area,16,16);
  check(fit_circle_selection(area,16,16,false,limits).kind=="ellipse","Exact area fit cap refused");
  --limits.max_working_bytes;refuses([&]{fit_circle_selection(area,16,16,false,limits);});
  refuses([&]{fit_circle_selection(Selection("multipoint",{{1,1},{3,3},{7,7}}),16,16);});
  refuses([&]{fit_circle_selection(Selection::from_packed_region(2,2,{0},{0,0}),16,16);});
  input=circlePoints;control.checkpoint=[&]{input.points={{0,0}};};
  check(fit_circle_selection(input,16,16,false,{},control).points==circleBounds,"Fit callback redirected captured points");
  const auto hole=Selection::from_path(std::vector<double>{0,3,3,1,13,3,1,13,13,1,3,13,4,0,5,5,1,11,5,1,11,11,1,5,11,4});
  check(fit_circle_selection(hole,16,16).points==std::vector<std::array<double,2>>{{3,3},{12,12}},"Area fit ignored path hole");
  std::cout<<"PASS Fit Circle point/area/hole geometry, caps, empty/singular and frozen callbacks\n";
  std::cout<<"PASS Convex Hull geometry, exact/short limits, point metadata policy and frozen callback input\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
