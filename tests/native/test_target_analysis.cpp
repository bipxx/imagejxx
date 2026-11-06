#include <imagejxx/target_analysis.hpp>
#include <imagejxx/line_measurement.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/selection_interpolation.hpp>
#include <imagejxx/selection_spline.hpp>
#include <imagejxx/imagej_roi_projection.hpp>
#include <imagejxx/roi_manager.hpp>
#include <imagejxx/planar_annotation_command.hpp>
#include <cmath>
#include <limits>
#include <iostream>
using namespace imagejxx;
namespace {
void check_at(bool ok,int line){if(!ok){std::cerr<<"Target analysis contract failed at line "<<line<<std::endl;throw std::runtime_error("Target analysis contract failed");}}
#define check(...) check_at((__VA_ARGS__),__LINE__)
template<class E,class F> void refuses(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught);}
struct Source final:PlaneSource {
  mutable std::vector<PlanePosition> reads;
  DatasetShape shape()const override{return {2,2,3,2,3,1};}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    reads.push_back(p);Image im(3,2,1,PixelType::UInt8);
    for(int i=0;i<6;++i)im.pixels[i]=60*p.t+20*p.c+5*p.z+i;
    return std::make_shared<const Image>(std::move(im));
  }
};
}
int main(){
  {
    // Pinned original ImageJ observations: ordinary fits, constructor branches,
    // empty processor reset versus retained zero-height mask and one-pixel ties.
    const double nan=std::numeric_limits<double>::quiet_NaN();
    struct EllipseCase {const char* id;const char* kind;std::vector<std::array<double,2>> points;bool fitted,traced;std::array<double,3> axes;std::array<double,2> center;};
    const std::vector<EllipseCase> cases={
      {"shape0-type0-cal0","whole_image",{},false,false,{45.13516668141449,45.13516668141449,0.0},{nan,nan}},
      {"shape1-type0-cal0","rectangle",{{3.0,3.0},{13.0,11.0}},false,false,{11.283791670353622,9.027033336282898,0.0},{nan,nan}},
      {"shape2-type0-cal0","ellipse",{{3.0,3.0},{13.0,11.0}},false,false,{10.299606058404963,7.91169394256132,0.0},{nan,nan}},
      {"shape3-type0-cal0","polygon",{{3.0,3.0},{13.0,3.0},{9.0,11.0}},false,false,{7.855171788226362,6.483573263889882,160.61047496058015},{nan,nan}},
      {"shape4-type0-cal0","polygon",{{3.0,3.0},{13.0,3.0},{13.0,7.0},{8.0,7.0},{8.0,11.0},{3.0,11.0}},false,false,{10.914536113038471,6.999323827684189,29.12642477130444},{nan,nan}},
      {"shape5-type0-cal0","rectangle",{{3.2,4.7},{13.399999999999999,13.0}},false,false,{12.412170837388985,10.15541250331826,0.0},{nan,nan}},
      {"shape6-type0-cal0","ellipse",{{3.2,4.7},{13.399999999999999,13.0}},false,false,{10.932218240412775,9.200870474964098,0.0},{nan,nan}},
      {"shape7-type0-cal0","rectangle",{{-2.7,-1.2},{2.3999999999999995,3.2}},false,false,{4.513516668141449,4.513516668141449,0.0},{nan,nan}},
      {"shape8-type0-cal0","rectangle",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{nan,nan}},
      {"shape9-type0-cal0","ellipse",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{nan,nan}},
      {"shape10-type0-cal0","rectangle",{{3.0,3.0},{4.0,4.0}},false,false,{1.1283791670353622,1.1283791670353622,0.0},{nan,nan}},
      {"shape11-type0-cal0","rectangle",{{3.0,3.0},{4.0,17.0}},false,false,{15.79730833849507,1.1283791670353622,90.0},{nan,nan}},
      {"shape12-type0-cal0","polygon",{{3.0,3.0},{4.0,3.0},{5.0,3.0}},false,false,{nan,nan,0.0},{nan,nan}},
      {"shape13-type0-cal0","polygon",{{3.0,3.0},{13.0,3.0},{13.0,13.0},{3.0,13.0}},true,false,{13.727325035155397,13.727325035155397,0.0},{nan,nan}},
      {"shape14-type0-cal0","freehand",{{3.200000047683716,4.699999809265137},{13.800000190734863,3.0999999046325684},{9.399999618530273,14.199999809265137}},false,true,{9.112088199735224,7.685195031635977,73.21130419733625},{nan,nan}},
      {"extra0-type0-shiftfalse-nanfalse-implicitfalse","whole_image",{},false,false,{45.13516668141449,45.13516668141449,0.0},{20.0,20.0}},
      {"extra1-type0-shiftfalse-nanfalse-implicitfalse","rectangle",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{0.0,0.0}},
      {"extra2-type0-shiftfalse-nanfalse-implicitfalse","ellipse",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{0.0,0.0}},
      {"extra3-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{60.0,60.0},{70.0,60.0},{65.0,68.0}},false,false,{0.0,0.0,0.0},{0.0,0.0}},
      {"extra4-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{3.0,3.0},{4.0,3.0},{5.0,3.0}},false,false,{nan,nan,0.0},{3.5,3.5}},
      {"extra5-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{3.0,3.0},{13.0,3.0},{9.0,11.0}},false,false,{7.855171788226362,6.483573263889882,160.61047496058015},{8.3,5.6}},
      {"extra6-type0-shiftfalse-nanfalse-implicitfalse","ellipse",{{3.0,3.0},{4.0,4.0}},false,false,{1.1283791670955126,1.1283791670955126,0.0},{3.5,3.5}},
      {"extra7-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{3.0999999046325684,3.0999999046325684},{3.200000047683716,3.0999999046325684},{3.1500000953674316,3.200000047683716}},false,false,{nan,nan,0.0},{3.5,3.5}},
      {"extra8-type0-shiftfalse-nanfalse-implicitfalse","rectangle",{{3.0,3.0},{4.0,17.0}},false,false,{15.79730833849507,1.1283791670353622,90.0},{3.5,10.0}}
    };
    for(const auto& value:cases)for(bool implicit:{false,true}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));
      if(std::string(value.kind)!="whole_image") {
        Selection s(value.kind,value.points);if(value.traced)s=with_selection_polygon_type(s,SelectionPolygonType::Traced);if(value.fitted)s=fit_spline_selection(s,40,40);owner->select(s);
      }
      Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;check(!options.ellipse);
      options.ellipse=!implicit;options.shape_descriptors=implicit;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0 && command.record().at("version")==MetadataValue(std::int64_t(15)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto snapshot=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      auto same=[&](double a,double b){if(!(std::isnan(b)?std::isnan(a):std::abs(a-b)<1e-10)){std::cerr<<value.id<<" implicit="<<implicit<<" actual="<<a<<" expected="<<b<<std::endl;check(false);}};
      if(!implicit){check(row.ellipse_major && row.ellipse_minor && row.ellipse_angle_degrees);same(*row.ellipse_major,value.axes[0]);same(*row.ellipse_minor,value.axes[1]);same(*row.ellipse_angle_degrees,value.axes[2]);check(!row.circularity);}
      else check(!row.ellipse_major && !row.ellipse_minor && !row.ellipse_angle_degrees && row.circularity);
      check(bool(row.centroid_world));if(!std::isnan(value.center[0])){same((*row.centroid_world)[0],value.center[0]);same((*row.centroid_world)[1],value.center[1]);}
      check(reads.counts().plane_reads==1 && owner->snapshot().shares_data(snapshot) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==1);
      auto control=ExecutionControl{};control.checkpoint=[] {throw OperationCancelled();};refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});check(reads.counts().plane_reads==1);
    }
    for(const auto* kind:{"rectangle","ellipse","polygon"}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));
      owner->select(Selection(kind,std::string(kind)=="polygon"?std::vector<std::array<double,2>>{{0,0},{1e100,0},{0,10}}:std::vector<std::array<double,2>>{{0,0},{1e100,10}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.ellipse=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
    {
      // BBox native coordinates remain wider than the legacy ellipse frame.
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));owner->select(Selection("rectangle",{{0,0},{1e100,10}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.bounding_rectangle=true;
      const auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
      check(*row.bounding_x==0 && *row.bounding_y==0 && *row.bounding_width==40 && *row.bounding_height==10 && reads.counts().plane_reads==1);
      options.ellipse=true;refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==1);
    }
    {
      DatasetReadObserver reads;SpatialCalibration spatial;spatial.spacing={2,.5,3};spatial.origin={7,-4,9};spatial.direction={1,1,0,0,1,0,.5,0,1};
      auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(Selection("polygon",{{3,3},{13,3},{9,11}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.ellipse=true;options.bounding_rectangle=true;options.feret=true;options.shape_descriptors=true;
      const auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
      check(*row.ellipse_major==0 && *row.ellipse_minor==0 && std::abs(*row.ellipse_angle_degrees-160.61047496058015)<1e-10);
      const auto center=spatial.voxel_to_world({8.3,5.6,0});for(int a=0;a<3;++a)check(std::abs((*row.centroid_world)[a]-center[a])<1e-12);
      check(row.bounding_x && row.feret && row.circularity && reads.counts().plane_reads==1);
    }
  }
  {
    struct RectangleCase {const char* kind;std::vector<std::array<double,2>> points;bool fitted,traced,invert;std::array<double,2> spacing,origin;std::array<double,4> expected;};
    const std::vector<RectangleCase> cases={
      {"whole_image",{},false,false,false,{1,1},{-0,-0},{0,0,40,40}},
      {"whole_image",{},false,false,false,{2,0.5},{-0,-0},{0,0,80,20}},
      {"whole_image",{},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,80,20}},
      {"whole_image",{},false,false,true,{2,0.5},{-0,19.5},{0,19.5,80,20}},
      {"rectangle",{{3,3},{13,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"rectangle",{{3,3},{13,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"rectangle",{{3,3},{13,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"rectangle",{{3,3},{13,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"ellipse",{{3,3},{13,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"ellipse",{{3,3},{13,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"ellipse",{{3,3},{13,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"ellipse",{{3,3},{13,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{1,1},{-0,-0},{3,4,11,9}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{2,0.5},{-0,-0},{6,2,22,4.5}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.75,22,4.5}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-0,19.5},{6,17.5,22,4.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{1,1},{-0,-0},{3,4,11,9}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{2,0.5},{-0,-0},{6,2,22,4.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.75,22,4.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-0,19.5},{6,17.5,22,4.5}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,false,{1,1},{-0,-0},{0,0,4,4}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,false,{2,0.5},{-0,-0},{0,0,8,2}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,8,2}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,true,{2,0.5},{-0,19.5},{0,19.5,8,2}},
      {"rectangle",{{60,60},{64,64}},false,false,false,{1,1},{-0,-0},{0,0,0,0}},
      {"rectangle",{{60,60},{64,64}},false,false,false,{2,0.5},{-0,-0},{0,0,0,0}},
      {"rectangle",{{60,60},{64,64}},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,0,0}},
      {"rectangle",{{60,60},{64,64}},false,false,true,{2,0.5},{-0,19.5},{0,19.5,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,false,{1,1},{-0,-0},{0,0,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,false,{2,0.5},{-0,-0},{0,0,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,true,{2,0.5},{-0,19.5},{0,19.5,0,0}},
      {"rectangle",{{3,3},{4,4}},false,false,false,{1,1},{-0,-0},{3,3,1,1}},
      {"rectangle",{{3,3},{4,4}},false,false,false,{2,0.5},{-0,-0},{6,1.5,2,0.5}},
      {"rectangle",{{3,3},{4,4}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,2,0.5}},
      {"rectangle",{{3,3},{4,4}},false,false,true,{2,0.5},{-0,19.5},{6,18,2,0.5}},
      {"rectangle",{{3,3},{4,17}},false,false,false,{1,1},{-0,-0},{3,3,1,14}},
      {"rectangle",{{3,3},{4,17}},false,false,false,{2,0.5},{-0,-0},{6,1.5,2,7}},
      {"rectangle",{{3,3},{4,17}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,2,7}},
      {"rectangle",{{3,3},{4,17}},false,false,true,{2,0.5},{-0,19.5},{6,18,2,7}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,false,{1,1},{-0,-0},{3,3,2,0}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,false,{2,0.5},{-0,-0},{6,1.5,4,0}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,4,0}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,true,{2,0.5},{-0,19.5},{6,18,4,0}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,false,{1,1},{-0,-0},{1,1,14,14}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,false,{2,0.5},{-0,-0},{2,0.5,28,7}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,true,{2,0.5},{-14.5,-1.75},{-12.5,-2.25,28,7}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,true,{2,0.5},{-0,19.5},{2,19,28,7}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,false,{1,1},{-0,-0},{3,3,11,12}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,false,{2,0.5},{-0,-0},{6,1.5,22,6}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,22,6}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,true,{2,0.5},{-0,19.5},{6,18,22,6}}
    };
    for(const auto& value:cases) {
      SpatialCalibration spatial;spatial.spacing={value.spacing[0],value.spacing[1],1};spatial.origin={value.origin[0],value.origin[1],0};if(value.invert)spatial.direction[4]=-1;
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));
      if(std::string(value.kind)!="whole_image"){Selection selection(value.kind,value.points);if(value.traced)selection=with_selection_polygon_type(selection,SelectionPolygonType::Traced);if(value.fitted)selection=fit_spline_selection(selection,40,40);owner->select(selection);}
      Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;check(!options.bounding_rectangle);options.bounding_rectangle=true;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0 && command.options().bounding_rectangle);
      check(command.record().at("version")==MetadataValue(std::int64_t(14)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.bounding_x && row.bounding_y && row.bounding_width && row.bounding_height && row.bounding_origin_world && row.bounding_horizontal_world && row.bounding_vertical_world);
      const std::array<double,4> actual{*row.bounding_x,*row.bounding_y,*row.bounding_width,*row.bounding_height};
      for(std::size_t i=0;i<4;++i){if(std::abs(actual[i]-value.expected[i])>1e-11)std::cerr<<value.kind<<" rectangle column "<<i<<" got "<<actual[i]<<" expected "<<value.expected[i]<<std::endl;check(std::abs(actual[i]-value.expected[i])<1e-11);}
      if(std::string(value.kind)=="polygon" && value.points.size()==3 && value.points[0][1]==value.points[1][1] && value.points[0][1]==value.points[2][1])check(!row.bounds && *row.bounding_width>0 && *row.bounding_height==0);
      check(!row.feret && !row.circularity && reads.counts().plane_reads==1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==1);
      options.shape_descriptors=true;options.feret=true;const auto combined=execute_target_analysis(resolve_target_analysis(command.target(),options));check(combined.planes.front().bounding_x==row.bounding_x && combined.planes.front().feret && combined.planes.front().circularity);
    }
    {
      SpatialCalibration spatial;spatial.spacing={2,.5,3};spatial.origin={7,-4,9};spatial.direction={1,1,0,0,1,0,.5,0,1};
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,2,40,40,1},{Image(40,40,1,PixelType::UInt8),Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(Selection("rectangle",{{3,3},{13,11}}));auto display=owner->snapshot().display();display.crosshair[2]=1;owner->set_display(display);Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.bounding_rectangle=true;const auto result=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options));const auto& row=result.planes.front();
      check(*row.bounding_origin_world==std::array<double,3>{14.5,-2.5,15} && *row.bounding_horizontal_world==std::array<double,3>{20,0,10} && *row.bounding_vertical_world==std::array<double,3>{4,4,0});
      check(std::abs(*row.bounding_width-std::sqrt(500.))<1e-11 && std::abs(*row.bounding_height-std::sqrt(32.))<1e-11 && row.position.z==1 && reads.counts().plane_reads==1);
    }
    {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_stack({Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));owner->select(Selection("polygon",{{0,0},{1e100,0},{0,10}}));Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.bounding_rectangle=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
  }
  {
    struct FeretCase {const char* kind;std::vector<std::array<double,2>> points;bool fitted,traced;std::array<double,2> spacing;std::array<double,5> expected;};
    const std::vector<FeretCase> cases={
      {"whole_image",{},false,false,{1,1},{56.568542494923804,40,135,0,0}},
      {"whole_image",{},false,false,{2,0.5},{56.568542494923804,40,135,0,0}},
      {"rectangle",{{3,3},{13,11}},false,false,{1,1},{12.806248474865697,8,141.34019174590992,3,3}},
      {"rectangle",{{3,3},{13,11}},false,false,{2,0.5},{20.396078054371138,4,168.69006752597977,3,3}},
      {"ellipse",{{3,3},{13,11}},false,false,{1,1},{10,8,0,3,7}},
      {"ellipse",{{3,3},{13,11}},false,false,{2,0.5},{20,4,0,6,3.5}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,{1,1},{10,8,126.86989764584402,3,3}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,{2,0.5},{20,4,0,3,3}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,{1,1},{12.806248474865697,8,38.659808254090095,3,11}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,{2,0.5},{20.396078054371138,4,11.309932474020215,3,11}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{1,1},{13.150285140452567,8.3000001907348633,140.86387018601474,3.2000000476837158,4.6999998092651367}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{2,0.5},{20.81784055149777,4.1500000953674316,168.50115063998516,3.2000000476837158,4.6999998092651367}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{1,1},{11,9,0,3,8.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{2,0.5},{22,4.5,0,6,4.25}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,{1,1},{6.7357259987491149,4.4000000953674316,139.21417870338706,-2.7000000476837158,-1.2000000476837158}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,{2,0.5},{10.43455872443937,2.2000000476837158,167.82854186669013,-2.7000000476837158,-1.2000000476837158}},
      {"rectangle",{{60,60},{64,64}},false,false,{1,1},{5.6568542494923806,4,135,60,60}},
      {"rectangle",{{60,60},{64,64}},false,false,{2,0.5},{8.2462112512353212,2,165.96375653207352,60,60}},
      {"ellipse",{{60,60},{64,64}},false,false,{1,1},{4,4,0,60,62}},
      {"ellipse",{{60,60},{64,64}},false,false,{2,0.5},{8,2,0,120,31}},
      {"rectangle",{{3,3},{4,4}},false,false,{1,1},{1.4142135623730951,1,135,3,3}},
      {"rectangle",{{3,3},{4,4}},false,false,{2,0.5},{2.0615528128088303,0.5,165.96375653207352,3,3}},
      {"rectangle",{{3,3},{4,17}},false,false,{1,1},{14.035668847618199,1,94.08561677997487,3,3}},
      {"rectangle",{{3,3},{4,17}},false,false,{2,0.5},{7.2801098892805181,2,105.94539590092285,3,3}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,{1,1},{2,0,0,3,3}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,{2,0.5},{4,0,0,3,3}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,{1,1},{14.139979053786409,13.742882792372066,45.000006148107872,3.0764639377593994,13.074938774108887}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,{2,0.5},{27.487765367547677,6.8715723147404244,179.88599433003395,1.1302516460418701,7.8330926895141602}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,{1,1},{11.940268569175016,9.264448738517812,68.376802837290896,9.3999996185302734,14.200000762939453}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,{2,0.5},{21.215089730480507,5.214213754450487,2.1610793304011708,3.2000000476837158,4.6999998092651367}}
    };
    for(const auto& value:cases) {
      SpatialCalibration spatial;spatial.spacing={value.spacing[0],value.spacing[1],1};spatial.origin={-14.5,3.5,0};spatial.unit="um";
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));
      const bool whole=std::string(value.kind)=="whole_image";
      if(!whole){Selection selection(value.kind,value.points);if(value.traced)selection=with_selection_polygon_type(selection,SelectionPolygonType::Traced);if(value.fitted)selection=fit_spline_selection(selection,40,40);owner->select(selection);}
      Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.feret=true;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0 && command.options().feret);
      check(command.record().at("version")==MetadataValue(std::int64_t(13)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.feret && row.min_feret && row.feret_angle && row.feret_x && row.feret_y);
      const std::array<double,5> actual{*row.feret,*row.min_feret,*row.feret_angle,*row.feret_x,*row.feret_y};
      for(std::size_t i=0;i<5;++i){if(std::abs(actual[i]-value.expected[i])>1e-10)std::cerr<<value.kind<<" Feret column "<<i<<" got "<<actual[i]<<" expected "<<value.expected[i]<<std::endl;check(std::abs(actual[i]-value.expected[i])<1e-10);}
      check(row.feret_in_pixels==whole && row.feret_coordinates_scaled==(std::string(value.kind)=="ellipse"));
      check(!row.circularity && reads.counts().plane_reads==1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==1);
      options.shape_descriptors=true;const auto combined=execute_target_analysis(resolve_target_analysis(command.target(),options));
      check(combined.planes.front().feret==row.feret && combined.planes.front().circularity.has_value());
    }
    for(const char* kind:{"rectangle","ellipse"}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_stack({Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));owner->select(Selection(kind,{{0,0},{1e100,10}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.feret=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
    for(const char* kind:{"rectangle","ellipse"}) {
      SpatialCalibration spatial;spatial.direction={1,1,0,0,1,0,0,0,1};
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(Selection(kind,{{3,3},{13,11}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.feret=true;
      const auto result=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options));const auto& row=result.planes.front();
      if(std::string(kind)=="rectangle"){check(std::abs(*row.feret-std::sqrt(388.))<1e-11);check(std::abs(*row.min_feret-std::sqrt(50.))<1e-11);}
      else {check(std::abs((*row.feret)*(*row.min_feret)-80.)<1e-10);check(*row.feret>10 && *row.min_feret>0);}
      check(reads.counts().plane_reads==1);
    }
  }
  {
    for(const char* kind:{"rectangle","ellipse"}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_stack({Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));
      owner->select(Selection(kind,{{0,0},{1e100,10}}));Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.shape_descriptors=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
  }
  {
    const double expected[][4]={{.7757018897752576,1.25,.8000000000852909,1},{.9998303296042818,1.3018205877502111,.7681550049290482,.9696969696969697},{.7144763353520187,1.559370073701773,.6412845910439401,.8571428571428571},{.4363323129985824,0,0,1},{0,0,0,0},{0,0,0,0},{.7775441817634738,1.2222222222222223,.8181818182690476,1.2375},{1,1,1,.9548387096774194}};
    for(int i=0;i<8;++i) {
      SpatialCalibration spatial;spatial.unit="um";if(i==3)spatial.spacing={2,.5,1};
      Selection selection(i==1 || i==4?"ellipse":i==2 || i==5 || i==7?"polygon":"rectangle",{{3,3},{13,11}});
      if(i==2)selection.points={{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}};
      if(i==4)selection.points={{60,60},{64,64}};
      if(i==5)selection.points={{3,3},{4,3},{5,3}};
      if(i==6)selection.points={{3.2,4.7},{13.4,13}};
      if(i==7)selection=fit_spline_selection(Selection("polygon",{{3,3},{13,3},{13,13},{3,13}}),40,40);
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(selection);Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.shape_descriptors=true;
      const auto before=reads.counts().plane_reads;const auto command=resolve_target_analysis(app.resolve_target(),options);check(command.record().at("version")==MetadataValue(std::int64_t(15)) && reads.counts().plane_reads==before);
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.circularity && row.aspect_ratio && row.roundness && row.solidity);
      check(std::abs(*row.circularity-expected[i][0])<1e-11);
      if(i==3)check(std::isnan(*row.aspect_ratio) && std::isinf(*row.roundness));
      else if(i==4 || i==5)check(std::isnan(*row.aspect_ratio) && std::isnan(*row.roundness));
      else check(std::abs(*row.aspect_ratio-expected[i][1])<1e-11 && std::abs(*row.roundness-expected[i][2])<1e-11);
      if(i==5)check(std::isnan(*row.solidity));else check(std::abs(*row.solidity-expected[i][3])<1e-11);
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==before+1);
    }
  }
  {
    SpatialCalibration spatial;spatial.spacing={2,.5,1};spatial.origin={-6.5,.75,0};spatial.unit="um";
    const PixelType types[]={PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float32,PixelType::UInt8};
    for(int i=0;i<5;++i) {
      Image pixels(8,6,1,types[i]);
      for(int y=0;y<6;++y)for(int x=0;x<8;++x)pixels.at(x,y)=i==0?x+2*y+1:i==3?2*(x%2)-1:i==4?x%2:0;
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,6,8,1},{pixels},spatial).observe_reads(reads));Application app;app.adopt(owner);
      if(i==4)app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{-1,2},"OD"),PixelType::UInt8));
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;const auto before=reads.counts().plane_reads;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(command.record().at("version")==MetadataValue(std::int64_t(11)) && reads.counts().plane_reads==before);
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.center_of_mass_world.has_value());const auto mass=*row.center_of_mass_world;
      if(i==0)check(std::abs(mass[0]-2.6052631578947363)<1e-12 && std::abs(mass[1]-2.557017543859649)<1e-12);
      else if(i==1)check(std::isnan(mass[0]) && std::isnan(mass[1]));
      else if(i==2)check(mass[0]==1.5 && mass[1]==2.25);
      else check(std::isinf(mass[0]) && !std::signbit(mass[0]) && std::isnan(mass[1]));
      check(mass[2]==0 && row.centroid_world && *row.centroid_world==std::array<double,3>{1.5,2.25,0});
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
    }
    refuses<std::invalid_argument>([&]{spatial.voxel_to_world({std::numeric_limits<double>::infinity(),0,0});});
  }
  {
    SpatialCalibration spatial;spatial.spacing={2,.5,1};spatial.origin={-6.5,.75,0};spatial.unit="um";
    for(int kind=0;kind<5;++kind) {
      DatasetReadObserver reads;DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads)};
      if(kind==1 || kind==2)data.selection=Selection(kind==1?"rectangle":"ellipse",{{3,3},{13,11}});
      if(kind==3)data.selection=Selection("polygon",{{3,3},{13,3},{9,11}});
      if(kind==4)data.selection=Selection("rectangle",{{60,60},{64,64}});
      auto owner=std::make_shared<Document>(std::move(data));Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
      const auto before=reads.counts().plane_reads;const auto command=resolve_target_analysis(app.resolve_target(),options);
      check(reads.counts().plane_reads==before && command.record().at("version")==MetadataValue(std::int64_t(11)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.centroid_world.has_value());const auto point=*row.centroid_world;
      if(kind==4)check(std::isnan(point[0]) && std::isnan(point[1]) && std::isnan(point[2]));
      else {const double x=kind==0?33.5:kind==3?10.1:9.5,y=kind==0?10.75:kind==3?3.55:4.25;check(std::abs(point[0]-x)<1e-12 && std::abs(point[1]-y)<1e-12 && point[2]==0);}
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
    }
  }
  {
    // Original Roi/OvalRoi/Analyzer observations,2/.5 um; run this maintained
    // native consumer before expensive Python module links.
    const char* kinds[]={"rectangle","ellipse","rectangle","ellipse","ellipse","whole_image"};
    const double expected[]={48.,42.01120851869869,53.,46.295306614080765,4.288751483104749,160.};
    SpatialCalibration spatial;spatial.spacing={2,.5,1};spatial.unit="um";
    for(int i=0;i<6;++i) {
      DatasetReadObserver reads;
      DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads)};
      if(i<5)data.selection=Selection(kinds[i],i==4?std::vector<std::array<double,2>>{{12.3,12.9},{12.5,13.7}}:i>=2?std::vector<std::array<double,2>>{{3.2,4.7},{13.4,13.0}}:std::vector<std::array<double,2>>{{3,3},{13,11}});
      auto owner=std::make_shared<Document>(std::move(data));Application application;application.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
      const auto before=reads.counts().plane_reads;const auto command=resolve_target_analysis(application.resolve_target(),options);
      check(reads.counts().plane_reads==before && command.record().at("version")==MetadataValue(std::int64_t(11)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();
      const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.perimeter && std::abs(*row.perimeter-expected[i])<1e-11);
      check(row.perimeter_in_pixels==(i==5) && row.length==std::nullopt);
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
    }
    DatasetReadObserver reads;auto huge=spatial;huge.spacing={1e308,1,1};
    DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},huge).observe_reads(reads)};
    data.selection=Selection("rectangle",{{3,3},{13,11}});auto owner=std::make_shared<Document>(std::move(data));Application application;application.adopt(owner);
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
    const auto before=reads.counts().plane_reads;refuses<std::invalid_argument>([&]{resolve_target_analysis(application.resolve_target(),options);});check(reads.counts().plane_reads==before);
  }
  {
    // Actual original Fit Spline command rows, shape1, Float constructor, 2/.5 um.
    const SelectionPolygonType types[]={SelectionPolygonType::Polygon,SelectionPolygonType::Freehand,SelectionPolygonType::Traced,SelectionPolygonType::Freeline};
    const double expected[]={52.65164241897764,41.50618389325088,0,20.89065609122347};
    SpatialCalibration calibration;calibration.spacing={2,.5,1};
    for(int i=0;i<4;++i) {
      auto source=with_selection_polygon_type(Selection(i==0?"polygon":i==3?"polyline":"freehand",{{3,3},{13,3},{13,13}}),types[i]);
      const auto fitted=fit_spline_selection(source,40,40);
      check(selection_spline_fit(fitted).has_value());
      check(std::abs(imagej_calibrated_polygon_length(fitted,calibration)-expected[i])<1e-6);
      DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},calibration)};
      data.selection=fitted;auto document=std::make_shared<Document>(std::move(data));Application application;application.adopt(document);
      TargetAnalysisOptions settings;settings.measurement=TargetMeasurementMode::Selection;
      const auto command=resolve_target_analysis(application.resolve_target(),settings);
      document->select(source);const auto result=execute_target_analysis(command);
      check(result.planes.front().perimeter && std::abs(*result.planes.front().perimeter-expected[i])<1e-6);
      check(command.record().at("version")==MetadataValue(std::int64_t(i==3?8:11)));
    }
  }
  {
    SpatialCalibration calibration;calibration.spacing={2,.5,1};
    const Selection path("polyline",{{3,3},{13,3},{13,13},{3,13}});
    auto free=with_selection_polygon_type(path,SelectionPolygonType::Freeline);
    check(std::abs(imagej_calibrated_polygon_length(free,calibration)-28.54085916099517)<1e-11);
    check(imagej_calibrated_polygon_length(path,calibration)==45);
    const auto traced=with_selection_polygon_type(Selection("freehand",path.points),SelectionPolygonType::Traced);
    check(std::isnan(imagej_calibrated_polygon_length(traced,calibration)));
    check(std::abs(imagej_calibrated_polygon_length(traced,calibration,true)-48.24621125123532)<1e-11);
    refuses<std::invalid_argument>([&]{imagej_calibrated_polygon_length(free,calibration,false,8255);});
    const auto integer=with_selection_polygon_coordinates(traced,true);
    check(selection_polygon_integer_coordinates(integer));
    check(!selection_polygon_integer_coordinates(SelectionEditor(integer,40,40).moved_vertex(0,{3.25,3})));
    DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},calibration)};
    data.selection=integer;auto document=std::make_shared<Document>(std::move(data));Application application;application.adopt(document);
    TargetAnalysisOptions settings;settings.measurement=TargetMeasurementMode::Selection;
    const auto command=resolve_target_analysis(application.resolve_target(),settings);
    document->select(with_selection_polygon_coordinates(integer,false));
    const auto result=execute_target_analysis(command);
    check(result.planes.front().perimeter && std::abs(*result.planes.front().perimeter-48.24621125123532)<1e-11);
    check(command.record().at("version")==MetadataValue(std::int64_t(11)));
  }
  {
    // Original lifecycle rows: fractional repeated closed fit, traced straighten,
    // and segmented direct-count refit; the same source is an installed SDK client.
    SpatialCalibration calibration;calibration.spacing={2,.5,1};
    const std::vector<std::array<double,2>> fractional={{3.2,4.7},{13.8,3.1},{19.4,14.2},{11.1,20.6},{4.3,13.8}};
    const auto first=fit_spline_selection(Selection("polygon",fractional),40,40);
    const auto repeated=fit_spline_selection(first,40,40);
    check(std::abs(imagej_calibrated_polygon_length(repeated,calibration)-74.14868185876702)<1e-7*74.14868185876702);
    SplineFitOptions straighten;straighten.mode=SplineFitMode::Straighten;
    const auto square=fit_spline_selection(Selection("polygon",{{3,3},{13,3},{13,13},{3,13}}),40,40);
    const auto straight=fit_spline_selection(square,40,40,straighten);
    check(std::abs(imagej_calibrated_polygon_length(straight,calibration)-48.24621125123532)<1e-7*48.24621125123532);
    SplineFitOptions seven;seven.evaluation_points=7;
    const auto path=fit_spline_selection(Selection("polyline",{{3,3},{13,3},{13,13}}),40,40);
    const auto counted=fit_spline_selection(path,40,40,seven);
    check(std::abs(imagej_calibrated_polygon_length(counted,calibration)-26.556521351852417)<1e-7*26.556521351852417);
  }
  auto provider=std::make_shared<Source>();auto owner=std::make_shared<Document>(Dataset(provider));
  Application app;app.adopt(owner);
  SpatialCalibration spatial;spatial.spacing={.5,2,3};spatial.unit="um";
  app.set_global_calibration(GlobalCalibration(spatial,
      ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},"OD"),PixelType::UInt8));
  TargetRequest request;request.scope=TargetScope::Positions;request.roi=RoiPolicy::WholePlane;
  request.positions={{1,1,2},{0,0,0}};
  TargetAnalysisOptions options;options.line=std::array<double,4>{0,0,2,1};
  const auto target=app.resolve_target(request);const auto estimate=estimate_target_analysis(target,options);
  check(estimate.plane_count==2 && estimate.total_pixels==12 && estimate.profile_samples==3 && provider->reads.empty());
  TargetAnalysisLimits limits;limits.max_working_bytes=estimate.working_bytes-1;
  refuses<std::invalid_argument>([&]{resolve_target_analysis(target,options,limits);});check(provider->reads.empty());
  limits.max_working_bytes=estimate.working_bytes;
  const auto command=resolve_target_analysis(target,options,limits);check(provider->reads.empty());
  options.line=std::array<double,4>{1,0,1,0}; // Cannot mutate the preparation.
  int calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;};
  refuses<std::invalid_argument>([&]{execute_target_analysis(command,control,estimate.working_bytes-1);});
  check(!calls && provider->reads.empty());
  const auto result=execute_target_analysis(command,control);
  check(calls>0 && provider->reads.size()==2 && provider->reads[0].t==1 && provider->reads[0].z==2);
  check(result.planes.size()==2 && result.planes[0].statistics->mean==195 && result.planes[1].statistics->mean==15);
  check(result.planes[1].profile_values==std::vector<double>({10,15,20}));
  check(result.profile_distances.size()==3 && std::abs(result.profile_distances.back()-std::sqrt(5.))<1e-12);
  check(owner->undo_steps()==0);app.validate_target(command.target());
  spatial.spacing[0]=2;app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8));
  refuses<RevisionConflict>([&]{app.validate_target(command.target());});
  check(execute_target_analysis(command).planes[1].statistics->mean==15); // Frozen value remains usable.
  auto token=std::make_shared<CancellationToken>();token->cancel();control.cancellation=token;calls=0;
  auto count=provider->reads.size();refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});
  check(!calls && provider->reads.size()==count);
  token=std::make_shared<CancellationToken>();control.cancellation=token;
  control.progress=[&](std::size_t done,std::size_t){if(done==1)token->cancel();};
  refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});check(provider->reads.size()==count+1);
  std::weak_ptr<Source> weak;TargetAnalysisResult detached;
  {auto source=std::make_shared<Source>();weak=source;Document temporary{Dataset(source)};
    TargetRequest whole;whole.roi=RoiPolicy::WholePlane;
    detached=execute_target_analysis(resolve_target_analysis(resolve_target(temporary.snapshot(),whole)));}
  check(weak.expired() && detached.planes[0].statistics->mean==2.5);
  check(detached.planes[0].bounds && *detached.planes[0].bounds==std::array<int,4>{0,0,3,2});
  {
    auto source=std::make_shared<Source>();Document temporary{Dataset(source)};
    TargetRequest whole;whole.roi=RoiPolicy::WholePlane;
    TargetAnalysisOptions configured;configured.statistics=false;
    configured.path={{0,0},{2,0},{2,1}};configured.samples_per_segment=3;
    const auto prepared=resolve_target_analysis(resolve_target(temporary.snapshot(),whole),configured);
    check(source->reads.empty() && prepared.estimate().profile_samples==5);
    const auto result=execute_target_analysis(prepared);
    check(source->reads.size()==1 && !result.planes[0].bounds);
    check(result.planes[0].profile_values==std::vector<double>({0,1,2,3.5,5}));
    check(result.profile_distances==std::vector<double>({0,1,2,2.5,3}));
    auto edit=temporary.begin_edit();edit.select(Selection("rectangle",{{1,0},{3,1}}));temporary.commit(edit);
    const auto area=execute_target_analysis(resolve_target_analysis(resolve_target(temporary.snapshot())));
    check(area.planes[0].bounds && *area.planes[0].bounds==std::array<int,4>{1,0,2,1});
    check(area.planes[0].statistics->mean==1.5);
    const auto measured=imagej_calibrated_line_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),0,0,2,1);
    check(measured.statistics.mean==2.5 && measured.statistics.stddev==2.5 && measured.statistics.area==3);
    check(std::abs(measured.length-std::sqrt(5.))<1e-12);
    check(measured.midpoint_world==std::array<double,3>{1,.5,0});
    refuses<std::invalid_argument>([&]{imagej_calibrated_line_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),0,0,2,1,true,1);});
    auto manager=temporary.begin_edit();
    manager.set_rois({Roi{"area",Selection("rectangle",{{0,0},{2,1}}),0,0,0,{}},
                      Roi{"line",Selection("line",{{0,0},{2,1}}),0,0,0,{}}});temporary.commit(manager);
    const auto request=roi_measurement_target_request(temporary.snapshot(),{1,0});
    TargetAnalysisOptions batch;batch.measurement=TargetMeasurementMode::RoiManager;batch.roi_indices={1,0};
    source->reads.clear();const auto prepared_rows=resolve_target_analysis(resolve_target(temporary.snapshot(),request),batch);
    check(source->reads.empty() && prepared_rows.estimate().plane_count==1 && prepared_rows.estimate().row_count==2);
    const auto rows=execute_target_analysis(prepared_rows);
    check(source->reads.size()==1 && rows.planes.size()==2);
    check(rows.planes[0].roi_index==1 && rows.planes[1].roi_index==0);
    check(rows.planes[0].statistics->mean==2.5 && rows.planes[1].statistics->mean==.5);
    check(rows.planes[0].length && rows.planes[0].midpoint_world==measured.midpoint_world);
    check(rows.record.at("version")==MetadataValue(std::int64_t(11)));
    const auto irregular=imagej_calibrated_polyline_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),{{0,0},{2,0},{2,1}});
    check(irregular.statistics.pixel_count==2 && irregular.statistics.mean==1.5 && irregular.length==3);
    check(imagej_line_bounds({0,0,2,1})==std::array<int,4>{0,0,3,2});
    check(std::abs(imagej_calibrated_angle({{{0,0},{1,1},{2,0}}},SpatialCalibration{},true)-270)<1e-12);
    auto angles=temporary.begin_edit();angles.set_rois({Roi{"angle",Selection("angle",{{0,0},{1,1},{2,0}}),0,0,0,{}}});temporary.commit(angles);
    batch.roi_indices={};source->reads.clear();
    const auto geometry=resolve_target_analysis(resolve_target(temporary.snapshot(),roi_measurement_target_request(temporary.snapshot())),batch);
    check(geometry.estimate().plane_count==0 && geometry.estimate().total_pixels==0);
    const auto angle_rows=execute_target_analysis(geometry);
    check(source->reads.empty() && angle_rows.planes[0].angle_degrees && std::abs(*angle_rows.planes[0].angle_degrees-90)<1e-12);
    auto points=temporary.begin_edit();points.set_rois({Roi{"points",Selection("multipoint",{{.5,.5},{0,0},{.5,.5}}),0,0,0,{}}});temporary.commit(points);
    const auto point_target=resolve_target(temporary.snapshot(),roi_measurement_target_request(temporary.snapshot()));
    TargetAnalysisLimits point_limits;point_limits.max_measurement_rows=2;
    refuses<std::invalid_argument>([&]{resolve_target_analysis(point_target,batch,point_limits);});
    check(source->reads.empty());
    const auto point_command=resolve_target_analysis(point_target,batch);
    check(point_command.estimate().row_count==3 && point_command.estimate().plane_count==1);
    const auto point_rows=execute_target_analysis(point_command);
    check(source->reads.size()==1 && point_rows.planes.size()==3);
    check(point_rows.planes[0].point_index==0 && point_rows.planes[2].point_index==2);
    check(point_rows.planes[0].point_world==std::array<double,3>{.5,.5,0});
    check(point_rows.planes[0].statistics->mean==4 && point_rows.planes[0].statistics->area==0 && point_rows.planes[0].statistics->integrated_density==4);
    const auto created=point_creation_annotation(source->shape(),{1,1,2},99,true);
    check(created.counter==99 && created.position->z==2);
    check(!point_creation_annotation(source->shape(),{1,1,2},99,false).position);
    PlanarAnnotation plane_points{"new",Selection("multipoint",{{1,1},{2,2}}),Plane::XZ,0,0,1,{}};
    const auto plane_created=planar_point_creation_selection(temporary.snapshot(),plane_points,7,true);
    const auto plane_entries=point_annotations(plane_created);
    check(plane_entries.size()==2 && plane_entries[0].counter==7 && plane_entries[0].position->t==0);
    check(plane_entries[0].position->c==1 && plane_entries[0].position->z==1 && plane_entries[1].position->z==2);
    check(!point_annotations(planar_point_creation_selection(temporary.snapshot(),plane_points,7,false))[0].position);
    check(!point_creation_annotation({1,1,1,2,2,1},{0,0,0}).position);
    refuses<std::invalid_argument>([&]{point_creation_annotation(source->shape(),{2,0,0},0,false);});
    const std::vector<std::uint32_t> words{256,3074,258};
    const auto annotated=with_imagej_point_counters(Selection("multipoint",{{0,0},{.5,.5},{0,0}}),words,source->shape());
    check(imagej_point_counters(annotated,source->shape())==words);
    check(point_annotations(annotated)[1].position->t==1 && point_annotations(annotated)[1].position->z==2);
    auto annotations=temporary.begin_edit();annotations.set_rois({Roi{"annotated",annotated,0,0,0,{}}});temporary.commit(annotations);
    source->reads.clear();const auto annotated_request=roi_measurement_target_request(temporary.snapshot());
    check(annotated_request.positions.size()==2);
    const auto annotation_command=resolve_target_analysis(resolve_target(temporary.snapshot(),annotated_request),batch);
    check(source->reads.empty());const auto annotation_rows=execute_target_analysis(annotation_command);
    check(source->reads.size()==2 && annotation_rows.planes[1].statistics->mean==94);
    check(annotation_rows.planes[1].point_counter==2 && annotation_rows.planes[1].point_counter_count==2);
    check(annotation_rows.record.at("version")==MetadataValue(std::int64_t(6)));
    const SelectionEditor point_editor(annotated,3,2);
    const auto inserted=point_editor.inserted_vertex(1,{2,1});
    check(imagej_point_counters(inserted,source->shape())==std::vector<std::uint32_t>({256,0,3074,258}));
    const auto removed=SelectionEditor(inserted,3,2).removed_vertex(1);
    check(removed==annotated && point_editor.selection()==annotated);
    SelectionEditLimits edit_limits;edit_limits.max_working_bytes=8192;
    refuses<std::length_error>([&]{point_editor.inserted_vertex(1,{2,1},edit_limits);});
    ImageJRoi raw;raw.roi={"points",without_point_annotations(annotated),{},{},0,{}};
    raw.point_counters=words;
    ImageJRoiPlaneMapping mapping;mapping.extent=source->shape();
    const auto bare=map_imagej_roi(raw,mapping);
    check(!bare.annotation().selection.attributes.count(point_annotations_attribute));
    const auto promoted=annotated_imagej_point_mapping(bare);
    check(promoted.annotation().selection==annotated);
    auto projected=imagej_roi_annotation_projection(promoted.annotation(),source->shape());
    check(projected.point_counters==words && !projected.roi.selection.attributes.count(point_annotations_attribute));
    const auto restored=decode_imagej_roi_envelope(encode_imagej_roi_native_envelope(promoted,projected));
    check(restored.annotation()==promoted.annotation() && restored.source_record().point_counters==words);
    auto imported_annotation=prepare_store_planar_annotation(temporary.snapshot(),promoted.annotation());
    temporary.commit(imported_annotation);
    const auto before_transfer=temporary.snapshot();
    auto adoption=prepare_select_planar_annotation(before_transfer,0);
    temporary.commit(adoption.transaction,std::move(adoption.display));
    check(temporary.snapshot().data().selection==annotated && source->reads.size()==2);
    auto stored=prepare_store_planar_annotation_roi(temporary.snapshot(),0);
    const auto new_index=*stored.row_index;temporary.commit(stored.transaction);
    check(temporary.snapshot().data().rois.at(new_index).selection==annotated);
    const auto copyback=resolve_planar_annotation_from_selection(temporary.snapshot(),0);
    validate_planar_annotation_command_record(copyback.record());
    refuses<std::invalid_argument>([&]{prepare_select_planar_annotation(temporary.snapshot(),0,8192);});
  }
  std::cout<<"target analysis: metadata admission, installed API, captured order/calibration, cancellation, detached results passed\n";
}
