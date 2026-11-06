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
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool ok){if(!ok)throw std::runtime_error("Target analysis contract failed");}
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
      check(command.record().at("version")==MetadataValue(std::int64_t(8)));
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
    check(command.record().at("version")==MetadataValue(std::int64_t(7)));
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
    check(rows.record.at("version")==MetadataValue(std::int64_t(3)));
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
