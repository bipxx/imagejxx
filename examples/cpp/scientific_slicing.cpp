// Installed public core-only consumer. Literal TCZYXS indices and complete
// affine/time landmarks are independently authored; no desktop/Python/UI deps.
#include <imagejxx/application.hpp>
#include <imagejxx/axis_extraction_command.hpp>
#include <imagejxx/session.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace imagejxx;
std::size_t checks = 0, finite_rows = 0, bit_rows = 0;
const auto started = std::chrono::steady_clock::now();
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
  ++checks;
  if (std::chrono::steady_clock::now()-started > std::chrono::seconds(45))
    throw std::runtime_error("SDK scientific slicing exceeded 45 seconds");
}
template<class Exception=std::invalid_argument, class F>
void rejects(F work, const char *message) {
  bool caught = false;
  try { work(); } catch (const Exception &) { caught = true; }
  require(caught, message);
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing={.5,1.25,2.5}; result.origin={11,-7,19};
  result.direction={-1,.25,.5,0,1,.5,.5,0,1};
  result.unit="um"; result.frame_interval=.75;
  result.time_origin=2.25; result.time_unit="s";
  return result;
}
std::array<int,3> axes(Plane plane) {
  if (plane==Plane::XY) return {0,1,2};
  if (plane==Plane::XZ) return {0,2,1};
  return {1,2,0};
}
const char *name(Plane plane) {
  return plane==Plane::XY ? "XY" : plane==Plane::XZ ? "XZ" : "YZ";
}
double literal(int t,int c,int z,int y,int x,int s) {
  return 64*t+24*c+8*z+3*y+x+2*s;
}
Dataset fixture(PixelType type,int samples=1) {
  Stack planes;
  for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<4;++z) {
    Image image(7,5,samples,type);
    for(int y=0;y<5;++y) for(int x=0;x<7;++x) for(int s=0;s<samples;++s)
      image.at(x,y,s)=literal(t,c,z,y,x,s);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({2,3,4,5,7,samples},std::move(planes),calibration());
}
DocumentData annotated(Dataset dataset,bool spectrum=true) {
  DocumentData result{std::move(dataset)};
  result.metadata.title="Scientific slicing SDK literal";
  result.metadata.properties["source_only"]=metadata_object({{"nested",metadata_array({true,7,std::string("retained")})}});
  result.metadata.extensions["future_annotation"]=metadata_object({{"version",37},{"opaque",metadata_array({3,1,4})}});
  result.metadata.record_layout["source_presence"]=true;
  result.selection=Selection("rectangle",{{1,1},{4,3}});
  result.previous_selection=Selection("line",{{0,0},{2,2}});
  result.rois={{"explicit zero",Selection("rectangle",{{1,1},{3,3}}),0,0,0,{}},
               {"wildcard",Selection("point",{{2,2}}),{},{},1,{}}};
  result.overlays={{{"name",std::string("source overlay")}}};
  if(spectrum)
    result.spectrum=FourierState(Spectrum{7,5,std::vector<std::complex<double>>(35,{1,0})},
                                 std::array<int,2>{5,7});
  return result;
}
std::shared_ptr<Document> owner(PixelType type=PixelType::UInt16,int samples=1,bool spectrum=true) {
  auto result=std::make_shared<Document>(annotated(fixture(type,samples),spectrum));
  auto edit=result->begin_edit(); edit.set_property("revision_probe",7); result->commit(edit);
  result->mark_saved();
  edit=result->begin_edit(); edit.set_property("history_probe",true); result->commit(edit); result->undo();
  auto view=result->snapshot().display();
  view.t=1; view.c=2; view.crosshair={3,2,2}; view.orthogonal=true;
  view.zoom=1.25; view.linkedZoom=false; view.planeZoom={1.25,2,3};
  view.pan={{{3,-2},{1,4},{-5,2}}}; result->set_display(view);
  return result;
}
void unchanged(const Document &source,const DocumentSnapshot &before,const HistoryInfo &history) {
  const auto now=source.snapshot(); const auto &a=now.display(),&b=before.display();
  require(now.shares_data(before) && now.data_identity()==before.data_identity() &&
          now.revisions().matches(before.revisions(),Change::All) && now.dirty()==before.dirty(),
          "SDK extraction changed source data/revisions/save identity");
  require(source.history().undo_steps==history.undo_steps && source.history().redo_steps==history.redo_steps &&
          source.history().step_limit==history.step_limit && source.history().pixel_budget==history.pixel_budget,
          "SDK extraction changed source undo/redo/policy");
  require(a.t==b.t && a.c==b.c && a.crosshair==b.crosshair && a.zoom==b.zoom &&
          a.planeZoom==b.planeZoom && a.pan==b.pan && a.linkedZoom==b.linkedZoom &&
          a.orthogonal==b.orthogonal && a.revision==b.revision && metadata_equal(a.properties,b.properties),
          "SDK extraction changed source navigation/cameras");
  for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<4;++z)
    require(now.data().dataset.plane({t,c,z})==before.data().dataset.plane({t,c,z}),
            "SDK extraction changed immutable source plane identity");
}
AxisExtractionRequest request(bool range,Plane plane,int t=1,int c=2) {
  const std::array<int,3> sizes{7,5,4};
  if(range) return AxisRangeResliceRequest{plane,1,sizes[axes(plane)[2]]-1,2,t,c};
  return AxisPlaneExportRequest{plane,2,t,c};
}
struct Parameters { Plane plane; int first,last,step,t,c; };
Parameters parameters(const AxisExtractionRequest &request) {
  return std::visit([](const auto &r)->Parameters {
    if constexpr(std::is_same_v<std::decay_t<decltype(r)>,AxisPlaneExportRequest>)
      return {r.plane,r.coordinate,r.coordinate,1,r.t,r.c};
    else return {r.plane,r.first,r.last,r.step,r.t,r.c};
  },request);
}
std::array<double,3> world(const SpatialCalibration &cal,const std::array<int,3> &voxel) {
  auto result=cal.origin;
  for(int r=0;r<3;++r) for(int a=0;a<3;++a)
    result[r]+=cal.direction[r*3+a]*cal.spacing[a]*voxel[a];
  return result;
}
void geometry(const Dataset &dataset,const AxisExtractionRequest &r,
              const SpatialCalibration &source,const AxisExtractionLayout *layout=nullptr) {
  const auto p=parameters(r); const auto order=axes(p.plane);
  const auto &cal=dataset.calibration(); const auto shape=dataset.shape();
  std::array<int,3> voxel{}; voxel[order[2]]=p.first;
  const auto origin=world(source,voxel);
  require(cal.origin==origin && cal.unit==source.unit && cal.time_unit==source.time_unit &&
          cal.time_origin==source.time_origin+source.frame_interval*p.t &&
          cal.frame_interval==source.frame_interval,"SDK extraction lost affine origin/time");
  for(int a=0;a<3;++a) {
    require(cal.spacing[a]==source.spacing[order[a]]*(a==2?p.step:1),"SDK axis spacing/stride mapping");
    for(int row=0;row<3;++row)
      require(cal.direction[row*3+a]==source.direction[row*3+order[a]],"SDK full direction-column mapping");
  }
  for(int k=0;k<shape.z;++k) for(int v:{0,shape.y-1}) for(int u:{0,shape.x-1}) {
    voxel[order[0]]=u; voxel[order[1]]=v; voxel[order[2]]=p.first+k*p.step;
    require(cal.voxel_to_world({double(u),double(v),double(k)})==world(source,voxel),
            "SDK analytic voxel-to-world landmark");
  }
  if(layout) {
    require(layout->source_axes==order && layout->plane_name==name(p.plane) &&
            layout->shape.dimensions()==shape.dimensions(),"SDK public layout shape/axis mapping");
    require(layout->geometry.origin==origin && layout->geometry.unit==source.unit &&
            layout->geometry.horizontal_axis==std::string(1,"XYZ"[order[0]]) &&
            layout->geometry.vertical_axis==std::string(1,"XYZ"[order[1]]),"SDK public source geometry labels/origin");
    for(int row=0;row<3;++row)
      require(layout->geometry.horizontal_step[row]==source.direction[row*3+order[0]]*source.spacing[order[0]] &&
              layout->geometry.vertical_step[row]==source.direction[row*3+order[1]]*source.spacing[order[1]],
              "SDK public source geometry independent basis vectors");
  }
}
void output(const Document &value,const DocumentSnapshot &source,const AxisExtractionRequest &request,
            const AxisExtractionLayout *layout=nullptr) {
  const auto snap=value.snapshot(); const auto &data=snap.data(); const auto &dataset=data.dataset;
  const auto p=parameters(request); const auto order=axes(p.plane);
  const std::array<int,3> extent{7,5,4}; const int count=1+(p.last-p.first)/p.step;
  const auto shape=dataset.shape();
  require(shape.dimensions()==std::array<int,6>{1,1,count,extent[order[1]],extent[order[0]],source.data().dataset.shape().s},
          "SDK exact output TCZYXS shape or C/S separation");
  require(dataset.pixel_type()==source.data().dataset.pixel_type() &&
          dataset.sample_policy()==source.data().dataset.sample_policy() &&
          !dataset.shares_pixels(source.data().dataset),"SDK exact output dtype/policy/new pixel ownership");
  for(int k=0;k<count;++k) {
    const auto image=dataset.plane({0,0,k});
    for(int y=0;y<shape.y;++y) for(int x=0;x<shape.x;++x) for(int s=0;s<shape.s;++s) {
      std::array<int,3> xyz{}; xyz[order[0]]=x; xyz[order[1]]=y; xyz[order[2]]=p.first+k*p.step;
      require(image->at(x,y,s)==literal(p.t,p.c,xyz[2],xyz[1],xyz[0],s),"SDK independent literal selected sample");
    }
  }
  require(value.id!=source.id() && !snap.dirty() && !value.undo_steps() && !value.redo_steps() &&
          !data.selection && !data.previous_selection && data.rois.empty() && data.overlays.empty() &&
          !data.spectrum && data.metadata.properties.empty() && data.metadata.record_layout.empty(),
          "SDK derived owner copied source annotation/history/save identity");
  const auto operation=std::holds_alternative<AxisPlaneExportRequest>(request)?"axis_plane_export":"axis_range_reslice";
  MetadataObject provenance{{"schema_version",std::int64_t(1)},{"source_document_id",source.id()},
      {"plane",std::string(name(p.plane))},{"t",std::int64_t(p.t)},{"c",std::int64_t(p.c)},
      {"horizontal_axis",std::string(1,"XYZ"[order[0]])},{"vertical_axis",std::string(1,"XYZ"[order[1]])},
      {"fixed_axis",std::string(1,"XYZ"[order[2]])},{"index_convention",std::string("zero_based_voxel_centers")}};
  if(std::holds_alternative<AxisPlaneExportRequest>(request)) provenance["coordinate"]=std::int64_t(p.first);
  else {
    provenance["first"]=std::int64_t(p.first); provenance["last"]=std::int64_t(p.last);
    provenance["step"]=std::int64_t(p.step); provenance["count"]=std::int64_t(count);
  }
  require(data.metadata.extensions.size()==1 && metadata_equal(data.metadata.extensions.at(operation),metadata_object(provenance)),
          "SDK exact source/request provenance");
  const auto title=source.data().metadata.title+' '+name(p.plane)+' '+std::to_string(p.first+1)+
      (std::holds_alternative<AxisPlaneExportRequest>(request)?std::string{}:
       '-'+std::to_string(p.last+1)+" step "+std::to_string(p.step));
  require(data.metadata.title==title,"SDK extraction generated title changed");
  require(snap.display().t==0 && snap.display().c==0 && !snap.display().orthogonal &&
          snap.display().crosshair[2]==0,"SDK ordinary output default native position");
  geometry(dataset,request,source.data().dataset.calibration(),layout);
}
const MetadataObject &object(const MetadataValue &value) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
MetadataObject replace(MetadataObject record,const char *outer,const char *inner,MetadataValue value) {
  auto nested=object(record.at(outer)); nested[inner]=std::move(value);
  record[outer]=metadata_object(std::move(nested)); return record;
}
void matrix() {
  for(bool range:{false,true}) for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ})
    for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64})
      for(int samples:{1,3,4}) {
        auto source=owner(type,samples); Application app; app.adopt(source);
        const auto before=source->snapshot(); const auto history=source->history();
        require(before.data().spectrum.has_value(),"SDK headless extraction fixture lost source Fourier state");
        auto r=request(range,plane); const auto command=resolve_axis_extraction_command(before,r);
        const auto record=command.record(); validate_axis_extraction_command_record(record);
        require(command.source().shares_data(before) && command.operation()==(range?"axis_range_reslice":"axis_plane_export"),
                "SDK immutable command source/operation");
        auto prepared=prepare_axis_extraction(command); const auto candidate=prepared.prepared_new_document();
        require(candidate && !app.contains(candidate->id) && prepared.active(),"SDK command preparation prematurely adopted owner");
        output(*candidate,before,r,&command.layout()); unchanged(*source,before,history);
        const auto published=app.publish(prepared);
        require(published.created && published.document==candidate && published.document_id==candidate->id &&
                app.document_ids()==std::vector<std::uint64_t>{source->id,candidate->id} &&
                app.active_document_id()==candidate->id && !prepared.active(),"SDK exact fresh-owner atomic publication");
        output(*published.document,before,r); unchanged(*source,before,history);
        require(metadata_equal(command.record(),record),"SDK preparation rewrote captured command record");
        app.close(candidate->id,true); require(app.document_ids()==std::vector<std::uint64_t>{source->id},"SDK disposable output close");
        ++finite_rows;
      }
  require(finite_rows==72,"SDK finite matrix lost declared membership");
}
std::uint32_t float_bits(double sample) {
  const float value=static_cast<float>(sample); std::uint32_t bits;
  std::memcpy(&bits,&value,sizeof(bits)); return bits;
}
void quiet_bits() {
  constexpr std::array<std::uint32_t,6> bits{0,0x80000000,1,0x7fc12345,0xffc34567,0x3f800000};
  Stack planes;
  for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<4;++z) {
    Image image(7,5,1,PixelType::Float32);
    for(int y=0;y<5;++y) for(int x=0;x<7;++x) {
      const auto pattern=bits[std::size_t(((((t*3+c)*4+z)*5+y)*7+x))%bits.size()];
      float value; std::memcpy(&value,&pattern,sizeof(value)); image.at(x,y)=static_cast<double>(value);
    }
    planes.push_back(std::move(image));
  }
  auto source=std::make_shared<Document>(Dataset::from_planes({2,3,4,5,7,1},std::move(planes),calibration(),
      FloatingSamplePolicy::PreserveQuietNaNFloat32));
  const auto before=source->snapshot(); const auto history=source->history();
  for(bool range:{false,true}) for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) {
    const auto r=request(range,plane); const auto p=parameters(r); const auto order=axes(plane);
    const auto command=resolve_axis_extraction_command(before,r); validate_axis_extraction_command_record(command.record());
    auto prepared=prepare_axis_extraction(command); const auto candidate=prepared.prepared_new_document();
    const auto &dataset=candidate->snapshot().data().dataset; const auto shape=dataset.shape();
    require(dataset.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32,"SDK extraction discarded quiet-NaN policy");
    for(int k=0;k<shape.z;++k) {
      const auto image=dataset.plane({0,0,k});
      for(int y=0;y<shape.y;++y) for(int x=0;x<shape.x;++x) {
        std::array<int,3> xyz{}; xyz[order[0]]=x; xyz[order[1]]=y; xyz[order[2]]=p.first+k*p.step;
        const auto index=std::size_t(((((p.t*3+p.c)*4+xyz[2])*5+xyz[1])*7+xyz[0]));
        require(float_bits(image->at(x,y))==bits[index%bits.size()],"SDK exact quiet-NaN payload/signed-zero/subnormal bits");
      }
    }
    ++bit_rows; unchanged(*source,before,history);
  }
  require(bit_rows==6,"SDK quiet-bit membership changed");
}
void replay_and_session() {
  SessionLimits limits; limits.max_documents=16; limits.max_encoded_bytes=limits.max_json_bytes=32*1024*1024;
  limits.max_working_bytes=128*1024*1024;
  {
    // Exact headless extraction admits/preserves this source state, while the
    // session format deliberately has no scientific Fourier position mapping.
    const auto unsupported=owner(); const auto before=unsupported->snapshot();
    const auto history=unsupported->history();
    rejects([&]{encode_session({{before},{},{},{}},limits);},
            "SDK session encoder accepted unsupported scientific Fourier position");
    unchanged(*unsupported,before,history);
  }
  for(bool range:{false,true}) {
    auto source=owner(PixelType::UInt16,1,false); Application app; app.adopt(source);
    require(!source->snapshot().data().spectrum,"SDK positive scientific session fixture contains Fourier state");
    const auto r=request(range,Plane::XZ); const auto original=resolve_axis_extraction_command(source->snapshot(),r).record();
    auto view=source->snapshot().display(); view.t=0; view.c=0; view.crosshair={1,1,0}; view.zoom=2.5;
    source->set_display(view); source->select(Selection("point",{{3,2}}));
    auto decoded=axis_extraction_command_from_record(source->snapshot(),original);
    require(metadata_equal(decoded.record(),original),"SDK navigation/selection changed strict replay record");
    auto signed_record=replace(original,"source","document_id",std::int64_t(source->id));
    validate_axis_extraction_command_record(signed_record);
    require(metadata_equal(axis_extraction_command_from_record(source->snapshot(),signed_record).record(),original),
            "SDK exact integer alternative changed command meaning");
    auto prepared=prepare_axis_extraction(decoded); output(*prepared.prepared_new_document(),source->snapshot(),r);
    const MetadataObject legacy{{"operation",std::string("add")},{"parameters",metadata_object({{"value",2}})}};
    const auto session=decode_session(encode_session({{source->snapshot()},{},
        {metadata_object(legacy),metadata_object(original)},{}},limits),limits);
    require(session.documents.size()==1 && session.workflow.size()==2 &&
            metadata_equal(session.workflow[0],metadata_object(legacy)) &&
            metadata_equal(session.workflow[1],metadata_object(original)),"SDK native session lost mixed workflow records");
    const auto reopened=session.documents[0];
    require(reopened->id!=source->id && !reopened->snapshot().dirty() && !reopened->undo_steps(),"SDK native session owner/save/history");
    rejects([&]{axis_extraction_command_from_record(reopened->snapshot(),original);},"SDK strict replay accepted reopened owner identity");
    auto new_cal=calibration(); new_cal.origin={-13,5,7}; new_cal.spacing={2,3,4}; new_cal.time_origin=31;
    auto edit=reopened->begin_edit(); edit.set_calibration(new_cal); reopened->commit(edit);
    const auto rebind_request=request(range,Plane::YZ,0,1);
    const auto rebound=rebind_axis_extraction_command(reopened->snapshot(),original,rebind_request);
    require(rebound.source().id()==reopened->id && parameters(rebound.request()).t==0 &&
            parameters(rebound.request()).c==1,"SDK explicit rebind reused old owner/T/C");
    auto rebind_prepared=prepare_axis_extraction(rebound);
    output(*rebind_prepared.prepared_new_document(),reopened->snapshot(),rebind_request,&rebound.layout());
    require(metadata_equal(original,resolve_axis_extraction_command(decoded.source(),r).record()),"SDK rebind mutated original record");
    edit=source->begin_edit(); edit.set_property("stale_metadata",true); source->commit(edit);
    rejects<RevisionConflict>([&]{axis_extraction_command_from_record(source->snapshot(),original);},"SDK stale metadata replay accepted");
    source->undo();
    rejects<RevisionConflict>([&]{axis_extraction_command_from_record(source->snapshot(),original);},"SDK reverted metadata revision replay accepted");
    const auto fresh=resolve_axis_extraction_command(source->snapshot(),r).record();
    edit=source->begin_edit(); Image replacement(7,5,1,PixelType::UInt16); edit.replace_plane({1,2,0},std::move(replacement)); source->commit(edit);
    rejects<RevisionConflict>([&]{axis_extraction_command_from_record(source->snapshot(),fresh);},"SDK stale pixel replay accepted");
  }
}
class CountingSource final : public PlaneSource {
public:
  mutable std::size_t reads=0;
  DatasetShape shape() const override { return {2,3,4,5,7,1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads; auto value=std::make_shared<Image>(7,5,1,PixelType::UInt16);
    for(int y=0;y<5;++y) for(int x=0;x<7;++x) value->at(x,y)=literal(p.t,p.c,p.z,y,x,0);
    return value;
  }
};
void metadata_and_controls() {
  auto provider=std::make_shared<CountingSource>();
  auto source=std::make_shared<Document>(Dataset(provider,calibration()));
  for(bool range:{false,true}) {
    const auto r=request(range,Plane::XZ); const auto command=resolve_axis_extraction_command(source->snapshot(),r);
    const auto original=command.record(); validate_axis_extraction_command_record(original);
    axis_extraction_command_from_record(source->snapshot(),original);
    require(provider->reads==0,"SDK metadata-only resolution/validation/replay read provider samples");
    std::size_t callbacks=0; ExecutionControl control;
    control.progress=[&](std::size_t,std::size_t){++callbacks;};
    rejects([&]{prepare_axis_extraction(command,control,0);},"SDK zero execution cap accepted");
    rejects([&]{prepare_axis_extraction(command,control,command.layout().working_bytes-1);},"SDK insufficient execution cap accepted");
    require(!provider->reads && !callbacks && metadata_equal(command.record(),original),"SDK cap refusal read/called/mutated record");
    auto bad=original; bad["version"]=true;
    rejects([&]{validate_axis_extraction_command_record(bad);},"SDK Boolean version accepted");
    bad=replace(original,"request",range?"step":"coordinate",true);
    rejects([&]{validate_axis_extraction_command_record(bad);},"SDK Boolean coordinate/step accepted");
    rejects([&]{rebind_axis_extraction_command(source->snapshot(),bad,r);},"SDK explicit rebind repaired malformed original");
    bad=replace(original,"derived","source_axes",metadata_array({0,1,2}));
    rejects([&]{validate_axis_extraction_command_record(bad);},"SDK contradictory redundant derived axes accepted");
    bad=replace(original,"source","document_id",std::uint64_t(0));
    rejects([&]{validate_axis_extraction_command_record(bad);},"SDK zero recorded source ID accepted");
    bad=original; bad["extra"]=7;
    rejects([&]{validate_axis_extraction_command_record(bad);},"SDK unknown command field accepted");
    require(!provider->reads && !callbacks,"SDK malformed metadata touched provider or controls");
    for(bool positive:{false,true}) {
      auto token=std::make_shared<CancellationToken>(); if(!positive) token->cancel();
      ExecutionControl cancellation; cancellation.cancellation=token;
      std::size_t progress_calls=0;
      cancellation.progress=[&](std::size_t done,std::size_t){++progress_calls;if(done)token->cancel();};
      rejects<OperationCancelled>([&]{prepare_axis_extraction(command,cancellation);},"SDK canceled preparation produced output");
      require(token->canceled() && (positive?progress_calls>1:progress_calls==0),"SDK bounded cancellation stage not observed");
    }
    // Positive cancellation above has now legitimately read the provider.
    provider->reads=0;
  }
  auto overflowing=calibration(); overflowing.spacing[2]=1e308; overflowing.origin[2]=1e308;
  rejects([&]{describe_axis_plane_export(DatasetShape{2,3,4,5,7,1},PixelType::UInt16,
      overflowing,AxisPlaneExportRequest{Plane::XY,2,1,2});},"SDK overflowing selected world coordinate accepted");
  auto request_overflow=AxisRangeResliceRequest{Plane::XY,0,3,1,1,2}; request_overflow.max_working_bytes=std::numeric_limits<std::size_t>::max();
  rejects([&]{describe_axis_range_reslice(DatasetShape{2,3,4,std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),4},PixelType::Float64,
      calibration(),request_overflow);},"SDK metadata-only pixel-byte overflow accepted");
}
void publication_refusals() {
  for(bool range:{false,true}) for(int reason=0;reason<6;++reason) {
    auto source=owner(); Application app; app.adopt(source);
    auto token=std::make_shared<CancellationToken>(); ExecutionControl control; control.cancellation=token;
    auto prepared=prepare_axis_extraction(resolve_axis_extraction_command(source->snapshot(),request(range,Plane::XZ)),control);
    const auto candidate=prepared.prepared_new_document();
    Application foreign; Application *publisher=&app;
    if(reason==0) {auto edit=source->begin_edit();edit.set_property("publication_conflict",true);source->commit(edit);}
    if(reason==1) {auto edit=source->begin_edit();Image image(7,5,1,PixelType::UInt16);edit.replace_plane({1,2,0},std::move(image));source->commit(edit);}
    if(reason==2) {auto edit=candidate->begin_edit();edit.set_property("candidate_conflict",true);candidate->commit(edit);}
    if(reason==3) app.close(source->id,true);
    if(reason==4) publisher=&foreign;
    if(reason==5) token->cancel();
    const auto before=source->snapshot(); const auto history=source->history(); const auto candidate_before=candidate->snapshot();
    const auto members=app.document_ids(); const auto publisher_members=publisher->document_ids();
    if(reason==3||reason==4) rejects<DocumentUnavailable>([&]{publisher->publish(prepared);},"SDK unavailable source published");
    else if(reason==5) rejects<OperationCancelled>([&]{publisher->publish(prepared);},"SDK canceled candidate published");
    else rejects<RevisionConflict>([&]{publisher->publish(prepared);},"SDK stale candidate/source published");
    require(prepared.active() && !app.contains(candidate->id) && app.document_ids()==members &&
            publisher->document_ids()==publisher_members && candidate->snapshot().shares_data(candidate_before),
            "SDK failed publication partially adopted or consumed candidate");
    unchanged(*source,before,history);
  }
}
} // namespace
int main(int argc,char **) {
  try {
    require(argc==1,"Scientific slicing SDK consumer accepts no expected-output arguments");
    matrix(); quiet_bits(); replay_and_session(); metadata_and_controls(); publication_refusals();
    std::cout<<"scientific_slicing: finite_rows="<<finite_rows<<" bit_rows="<<bit_rows<<" checks="<<checks<<'\n';
    return 0;
  } catch(const std::exception &error) {
    std::cerr<<"scientific_slicing: "<<error.what()<<'\n'; return 1;
  }
}
