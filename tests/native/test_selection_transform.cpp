#include <imagejxx/selection_transform.hpp>
#include <imagejxx/selection_spline.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/selection_creation.hpp>
#include <iostream>
#include <stdexcept>
#include <cmath>
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
  const auto ellipse=fit_ellipse_selection(area,16,16);const auto frame=*ellipse.box_frame();
  check(ellipse.kind=="rotated_ellipse" && frame.center==std::array<double,2>{8,8} && std::abs(frame.u[0]-10/1.772453851)<1e-12,"Rectangle moment ellipse differs");
  const auto pointEllipse=fit_ellipse_selection(circlePoints,16,16);
  check(pointEllipse.points[0]==std::array<double,2>{8.5,8.5},"Point fit must use pixel centers, not circle vertices");
  limits={};limits.max_working_bytes=estimate_fit_ellipse_selection(area,16,16);
  check(fit_ellipse_selection(area,16,16,false,limits)==ellipse,"Exact ellipse cap refused");
  --limits.max_working_bytes;refuses([&]{fit_ellipse_selection(area,16,16,false,limits);});
  refuses([&]{fit_ellipse_selection(Selection("polyline",{{1,1},{2,2},{4,1}}),16,16);});
  refuses([&]{fit_ellipse_selection(Selection::from_packed_region(2,2,{0},{0,0}),16,16);});
  input=area;control.checkpoint=[&]{input.points={{0,0},{1,1}};};
  check(fit_ellipse_selection(input,16,16,false,{},control)==ellipse,"Ellipse callback redirected source");
  std::cout<<"PASS Fit Ellipse pixel moments, point convention, caps, line/empty and frozen callbacks\n";
  Document ellipseOwner(Dataset::from_stack({Image(16,16,1,PixelType::UInt8)}));
  refuses([&]{resolve_selection_creation_command(ellipseOwner.snapshot(),{SelectionCreationOperation::Replace,ellipse});});
  const auto ellipseCommand=resolve_selection_creation_command(ellipseOwner.snapshot(),{SelectionCreationOperation::Replace,ellipse,SelectionCreationGeometry::NativePathV4});
  validate_selection_creation_command_record(ellipseCommand.record());
  auto ellipseEdit=prepare_selection_creation(ellipseCommand);ellipseOwner.commit(ellipseEdit);
  check(ellipseOwner.snapshot().data().selection==std::optional<Selection>(ellipse) && ellipseOwner.undo_steps()==1,"V4 ellipse candidate lost geometry/history");
  ellipseOwner.undo();check(!ellipseOwner.snapshot().data().selection,"Ellipse candidate Undo lost empty source");
  const auto fittedRectangle=fit_rectangle_selection(area,16,16);
  check(fittedRectangle.kind=="rotated_rectangle" && fittedRectangle.points==std::vector<std::array<double,2>>{{8,8},{8,3},{13,8}},"Minimum rectangle frame differs");
  limits={};limits.max_working_bytes=estimate_fit_rectangle_selection(area,16,16);
  check(fit_rectangle_selection(area,16,16,false,limits)==fittedRectangle,"Exact minimum rectangle cap refused");
  --limits.max_working_bytes;refuses([&]{fit_rectangle_selection(area,16,16,false,limits);});
  refuses([&]{fit_rectangle_selection(Selection("line",{{1,1},{4,4}}),16,16);});
  refuses([&]{fit_rectangle_selection(Selection("multipoint",{{1,1},{4,4},{8,8}}),16,16);});
  input=area;control.checkpoint=[&]{input.points={{0,0},{1,1}};};
  check(fit_rectangle_selection(input,16,16,false,{},control)==fittedRectangle,"Rectangle callback redirected source");
  refuses([&]{resolve_selection_creation_command(ellipseOwner.snapshot(),{SelectionCreationOperation::Replace,fittedRectangle});});
  const auto rectangleCommand=resolve_selection_creation_command(ellipseOwner.snapshot(),{SelectionCreationOperation::Replace,fittedRectangle,SelectionCreationGeometry::NativePathV4});
  validate_selection_creation_command_record(rectangleCommand.record());
  auto rectangleEdit=prepare_selection_creation(rectangleCommand);ellipseOwner.commit(rectangleEdit);
  check(ellipseOwner.snapshot().data().selection==std::optional<Selection>(fittedRectangle),"V4 rectangle lost native geometry");
  std::cout<<"PASS Fit Rectangle minimum-area frame,limits,collinearity,frozen callbacks and V4 candidate\n";
  const Selection splineControls("polygon",{{3,3},{13,3},{13,13},{3,13}});
  const auto spline=fit_spline_selection(splineControls,16,16);
  check(spline.points.size()==100 && selection_spline_fit(spline)->controls==splineControls,"Spline lost source knots/default samples");
  const SelectionEditor splineEditor(spline,16,16);check(splineEditor.handles().size()==4,"Spline exposes sampled vertices instead of knots");
  const auto refit=splineEditor.moved_vertex(1,{14,4});
  check(selection_spline_fit(refit)->controls.points[1]==std::array<double,2>{14,4},"Spline knot edit detached fit");
  check(remove_spline_fit(spline,16,16)==splineControls,"Spline removal lost controls");
  const auto splineCommand=resolve_selection_creation_command(ellipseOwner.snapshot(),{SelectionCreationOperation::Replace,spline,SelectionCreationGeometry::NativePathV4});
  validate_selection_creation_command_record(splineCommand.record());
  refuses([&]{resolve_selection_creation_command(ellipseOwner.snapshot(),{SelectionCreationOperation::Replace,spline});});
  auto freeSpline=fit_spline_selection(Selection("freehand",{{2.2,3.7},{12.8,3.1},{5.5,13.2}}),16,16);
  const auto insertedFree=SelectionEditor(freeSpline,16,16).inserted_vertex(1,{7,2});
  check(selection_spline_fit(insertedFree)->controls.points.size()==4 && selection_spline_fit(insertedFree)->controls.kind=="freehand","Freehand insertion reapplied command trimming");
  const auto removedFree=remove_spline_fit(freeSpline,16,16);ExecutionControl removeControl;
  removeControl.checkpoint=[&]{freeSpline=Selection("point",{{0,0}});};
  check(remove_spline_fit(freeSpline,16,16,{},removeControl)==removedFree,"Remove callback redirected frozen fitted samples");
  std::cout<<"PASS Spline numeric/state,real knot refit,removal and explicit V4 candidate\n";
  std::cout<<"PASS Convex Hull geometry, exact/short limits, point metadata policy and frozen callback input\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
