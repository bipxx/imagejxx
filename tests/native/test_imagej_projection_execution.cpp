#include <imagejxx/imagej_projection_execution.hpp>
#include <imagejxx/imagej_projection_command.hpp>
#include <imagejxx/imagej_intensity_execution.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/session.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check_impl(bool v,int line) { if(!v) throw std::runtime_error("Prepared projection assertion at line "+std::to_string(line)); }
#define check(...) check_impl((__VA_ARGS__),__LINE__)
template<class Error=std::invalid_argument,class F> void reject(F f) {
  try { f(); } catch(const Error&) { return; }
  throw std::runtime_error("Invalid prepared projection accepted");
}
std::shared_ptr<Document> source(int samples=1) {
  DatasetShape shape{2,2,4,1,2,samples};
  Stack planes;
  for(int t=0;t<2;++t) for(int c=0;c<2;++c) for(int z=0;z<4;++z) {
    Image image(2,1,samples,PixelType::UInt16);
    for(int x=0;x<2;++x) for(int s=0;s<samples;++s) image.at(x,0,s)=100*t+10*c+z+x;
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes)),{},{},{},{},{},{}};
  data.metadata.title="scientific";
  data.metadata.properties["stale_note"]=std::string("source only");
  data.overlays={{{"name",std::string("global")}},
      {{"t",1},{"c",1},{"z",2},{"slice_index",2},{"name",std::string("in")}},
      {{"z",0},{"name",std::string("out")}}};
  data.selection=Selection("rectangle",{{0,0},{1,1}});
  data.previous_selection=data.selection;
  auto doc=std::make_shared<Document>(std::move(data));
  auto display=doc->snapshot().display();
  ChannelDisplayState state;state.channels.resize(2);
  state.channels[0].lut.name="Red";state.channels[0].range={11,111};
  state.channels[1].lut.name="Green";state.channels[1].range={22,222};
  state.channels[1].visible=false;
  display=with_channel_display(display,doc->snapshot().data().dataset,state);
  display.t=1;display.c=1;display.crosshair[2]=2;
  doc->set_display(display);
  set_imagej_intensity_calibration(*doc,ImageJIntensityCalibration::signed16("mV"));
  return doc;
}
ImageJProjectionDocumentRequest request() {
  ImageJProjectionDocumentRequest r;r.projection.first=1;r.projection.last=3;
  r.projection.active_t=1;r.projection.active_c=1;r.projection.method="maximum";
  return r;
}
void unchanged(const DocumentSnapshot& before,const std::shared_ptr<Document>& doc) {
  auto now=doc->snapshot();check(now.shares_data(before));
  check(now.revisions().matches(before.revisions(),Change::All));
}
}
int main() try {
  auto doc=source();Application app;app.adopt(doc);
  const auto before=doc->snapshot();const auto history=doc->undo_steps();
  auto r=request();auto prepared=prepare_imagej_projection(before,r);
  auto candidate=prepared.prepared_new_document();check(bool(candidate));
  check(prepared.active() && app.document_ids().size()==1);
  const auto output=candidate->snapshot();
  const auto outshape=output.data().dataset.shape();
  check(outshape.t==2 && outshape.c==2 && outshape.z==1 && outshape.y==1 && outshape.x==2 && outshape.s==1);
  check(output.data().metadata.title=="MAX_scientific");
  check(!output.data().selection && !output.data().previous_selection && output.data().rois.empty());
  check(output.data().metadata.properties.count("stale_note")==0);
  check(output.data().dataset.plane({1,1,0})->pixels[0]==113);
  check(output.display().t==0 && output.display().c==0 && output.display().crosshair[2]==0);
  const auto channels=recognized_channel_display(output.display(),output.data().dataset);
  check(channels && channels->channels[1].lut.name=="Green" && !channels->channels[1].visible);
  check(channels->channels[1].range==std::array<double,2>{22,222});
  check(read_imagej_intensity_calibration(output).shares_record(read_imagej_intensity_calibration(before)));
  check(output.data().overlays.size()==2);
  check(std::get<std::int64_t>(output.data().overlays[1].at("z"))==0);
  check(candidate->undo_steps()==0 && !output.dirty());
  unchanged(before,doc);check(doc->undo_steps()==history);
  const auto published=app.publish(prepared);check(published.created && published.document==candidate);
  check(!prepared.active() && app.document_ids().size()==2);
  unchanged(before,doc);
  reject([&]{app.publish(prepared);});
  SessionCapture capture;capture.documents.push_back(candidate->snapshot());
  auto reopened=decode_session(encode_session(capture));
  check(reopened.documents.size()==1);
  check(read_imagej_intensity_calibration(reopened.documents[0]->snapshot()).value_unit()=="mV");
  check(recognized_channel_display(reopened.documents[0]->snapshot().display(),
      reopened.documents[0]->snapshot().data().dataset)->channels[1].lut.name=="Green");
  auto without_channels=source();
  auto legacy_display=with_channel_display(without_channels->snapshot().display(),
      without_channels->snapshot().data().dataset,std::nullopt);
  without_channels->set_display(legacy_display);
  auto defaults=prepare_imagej_projection(without_channels->snapshot(),request());
  const auto defaults_out=defaults.prepared_new_document()->snapshot();
  const auto defaults_state=recognized_channel_display(defaults_out.display(),defaults_out.data().dataset);
  check(defaults_state->channels[0].lut.name=="Red" && defaults_state->channels[1].lut.name=="Green");
  check(defaults_state->channels[0].range==std::array<double,2>{3,4});
  check(defaults_state->channels[1].range==defaults_state->channels[0].range);
  auto current=request();current.projection.all_time_frames=false;
  auto current_result=prepare_imagej_projection(before,current);
  const auto current_out=current_result.prepared_new_document()->snapshot();
  // Pinned hyper.u16.z4.allfalse.max: wildcard C survives an out-of-range Z;
  // an included current-frame ROI becomes channel-only, with no source T/Z.
  check(current_out.data().overlays.size()==3);
  check(current_out.data().overlays[1].count("c")==1);
  check(current_out.data().overlays[1].count("t")==0 && current_out.data().overlays[1].count("z")==0);
  check(current_out.data().overlays[2].count("z")==0);
  Stack ordinary_planes;
  for(int z=0;z<4;++z) { Image image(1,1,1,PixelType::UInt8);image.pixels={double(z)};
    ordinary_planes.push_back(std::move(image)); }
  DocumentData ordinary_data{Dataset::from_planes({1,1,4,1,1,1},std::move(ordinary_planes)),{},{},{},{},{},{}};
  ordinary_data.overlays={{{"name",std::string("global")}},{{"slice_index",1}},{{"slice_index",0}}};
  Document ordinary(std::move(ordinary_data));auto ordinary_request=request();
  ordinary_request.projection.active_t=0;ordinary_request.projection.active_c=0;
  auto ordinary_result=prepare_imagej_projection(ordinary.snapshot(),ordinary_request);
  const auto ordinary_out=ordinary_result.prepared_new_document()->snapshot();
  // Pinned scalar.u8.range_even.max: retain original flat2 on a one-plane
  // output. It remains hidden rather than being remapped to visible plane1.
  check(ordinary_out.data().overlays.size()==2);
  check(std::get<std::int64_t>(ordinary_out.data().overlays[1].at("slice_index"))==1);
  r.projection.method="sum";
  auto sum=prepare_imagej_projection(before,r);
  const auto sum_output=sum.prepared_new_document()->snapshot();
  const auto sum_intensity=read_imagej_intensity_calibration(sum_output);
  check(!sum_intensity.has_density() && sum_intensity.value_unit()=="mV");
  auto sum_channels=recognized_channel_display(sum_output.display(),sum_output.data().dataset);
  check(sum_channels->version==ChannelDisplayVersion::V3);
  check(sum_channels->channels[0].range==std::array<double,2>{-98298,-98295});
  check(sum_channels->channels[1].range==std::array<double,2>{-98268,-98265});
  auto edit=doc->begin_edit();edit.set_property("changed",true);doc->commit(edit);
  reject<RevisionConflict>([&]{app.publish(sum);});check(app.document_ids().size()==2 && sum.active());
  auto candidate_edit=prepare_imagej_projection(doc->snapshot(),request());
  auto changed=candidate_edit.prepared_new_document();auto tx=changed->begin_edit();
  tx.set_title("changed");changed->commit(tx);
  reject<RevisionConflict>([&]{app.publish(candidate_edit);});
  auto display_edit=prepare_imagej_projection(doc->snapshot(),request());
  auto display=doc->snapshot().display();display.c=0;doc->set_display(display);
  reject<RevisionConflict>([&]{app.publish(display_edit);});
  auto token=std::make_shared<CancellationToken>();ExecutionControl control;control.cancellation=token;
  auto canceled=prepare_imagej_projection(doc->snapshot(),request(),control);token->cancel();
  reject<OperationCancelled>([&]{app.publish(canceled);});
  reject<OperationCancelled>([&]{prepare_imagej_projection(doc->snapshot(),request(),control);});
  auto closed=prepare_imagej_projection(doc->snapshot(),request());app.close(doc->id,true);
  reject<DocumentUnavailable>([&]{app.publish(closed);});
  auto alias=source();auto captured=alias->snapshot();auto mutable_request=request();
  bool once=false;ExecutionControl retarget;
  retarget.checkpoint=[&]{if(!once){once=true;captured=candidate->snapshot();mutable_request.projection.last=0;
    mutable_request.projection.method="sum";}};
  const auto alias_id=alias->id;
  auto isolated=prepare_imagej_projection(captured,mutable_request,retarget);
  check(isolated.target().document_id()==alias_id);
  check(isolated.prepared_new_document()->snapshot().data().metadata.title=="MAX_scientific");
  check(isolated.target().ranges()[2].start==1 && isolated.target().ranges()[2].stop==4);
  // Explicit nonzero Time/Channels bounds must bind exactly those source
  // contributors, independently of captured display navigation.
  for(auto axis:{ImageJProjectionAxis::Time,ImageJProjectionAxis::Channels}) {
    auto a=request();a.projection.axis=axis;a.projection.first=1;a.projection.last=1;
    auto projected=prepare_imagej_projection(alias->snapshot(),a);
    const auto& ranges=projected.target().ranges();
    const int reduced=axis==ImageJProjectionAxis::Time?0:1;
    check(ranges[reduced].start==1 && ranges[reduced].stop==2);
    const auto& items=projected.prepared_new_document()->snapshot().data().overlays;
    check(items.size()==3);
    check(std::get<std::int64_t>(items[1].at(reduced==0?"t":"c"))==0);
  }
  auto grouped=request();grouped.projection.first=0;grouped.projection.last=3;
  grouped.projection.group_size=2;
  auto group=prepare_imagej_projection(alias->snapshot(),grouped);
  check(group.prepared_new_document()->snapshot().data().dataset.shape().z==2);
  // RGB retains its interleaved sample axis and uses ordinary fresh RGB
  // presentation, with no scalar density/channel-display interpretation.
  Stack rgb_planes;
  for(int z=0;z<2;++z) { Image image(1,1,3,PixelType::UInt8);
    image.pixels={double(10+z),double(20+z),double(30+z)};rgb_planes.push_back(std::move(image)); }
  Document rgb(Dataset::from_planes({1,1,2,1,1,3},std::move(rgb_planes)),"RGB");
  auto rgb_request=request();rgb_request.projection.first=0;rgb_request.projection.last=1;
  rgb_request.projection.active_t=0;rgb_request.projection.active_c=0;
  auto rgb_result=prepare_imagej_projection(rgb.snapshot(),rgb_request);
  const auto rgb_out=rgb_result.prepared_new_document()->snapshot();
  check(rgb_out.data().dataset.shape().s==3 && rgb_out.data().dataset.plane()->pixels[2]==31);
  check(rgb_out.display().properties.empty() && !read_imagej_intensity_calibration(rgb_out).has_density());
  auto invalid=request();invalid.projection.signed16_density=true;
  reject([&]{prepare_imagej_projection(alias->snapshot(),invalid);});
  invalid=request();invalid.display_limits.max_pixels=1;
  reject([&]{prepare_imagej_projection(alias->snapshot(),invalid);});
  auto bad=alias->begin_edit();bad.set_overlays({{{"z",true}}});alias->commit(bad);
  reject([&]{prepare_imagej_projection(alias->snapshot(),request());});
  class MetadataOnlySource final:public PlaneSource {
  public:
    mutable int reads=0;
    DatasetShape shape() const override {return {1,1024,2,1,1,1};}
    PixelType pixel_type() const override {return PixelType::UInt16;}
    std::shared_ptr<const Image> read_plane(PlanePosition) const override {
      ++reads;throw std::runtime_error("Admission must not read provider pixels");
    }
  };
  auto provider=std::make_shared<MetadataOnlySource>();
  Document many_channels(Dataset(provider),"many channels");
  auto bounded=request();bounded.projection.first=0;bounded.projection.last=1;
  bounded.projection.active_t=0;bounded.projection.active_c=0;
  bounded.max_working_bytes=16*1024*1024;
  reject([&]{resolve_imagej_projection_command(many_channels.snapshot(),bounded);});
  check(provider->reads==0);
  bounded.max_working_bytes=32*1024*1024;
  const auto admitted=resolve_imagej_projection_command(many_channels.snapshot(),bounded);
  check(admitted.estimate().working_bytes>16*1024*1024 && provider->reads==0);
  auto raw=imagej_projection_request_record(request());
  auto strict_request=imagej_projection_request_from_record(raw);
  const auto strict_source=without_channels->snapshot();
  const auto command=resolve_imagej_projection_command(strict_source,strict_request);
  validate_imagej_projection_command_record(command.record());
  check(command.estimate().working_bytes>command.estimate().layout.working_bytes);
  auto rebound=imagej_projection_command_from_record(strict_source,command.record());
  check(metadata_equal(rebound.record(),command.record()));
  auto low=strict_request;low.max_working_bytes=command.estimate().working_bytes-1;
  reject([&]{resolve_imagej_projection_command(strict_source,low);});
  auto malformed=raw;malformed["first"]=true;
  reject([&]{imagej_projection_request_from_record(malformed);});
  malformed=raw;malformed["max_working_bytes"]=0;
  reject([&]{imagej_projection_request_from_record(malformed);});
  auto corrupt=command.record();corrupt["version"]=true;
  reject([&]{validate_imagej_projection_command_record(corrupt);});
  reject([&]{rebind_imagej_projection_command(strict_source,corrupt,strict_request);});
  corrupt=command.record();corrupt["derived"]=metadata_object({});
  reject([&]{validate_imagej_projection_command_record(corrupt);});
  auto limited=strict_request;limited.max_record_bytes=1;
  reject([&]{resolve_imagej_projection_command(strict_source,limited);});
  auto text_limited=raw;text_limited["max_record_bytes"]=64;
  text_limited["output_title"]=std::string(1024,'x');
  reject([&]{imagej_projection_request_from_record(text_limited);});
  auto oversized=request();oversized.output_title=std::string(1024*1024+1,'x');
  reject([&]{imagej_projection_request_record(oversized);});
  auto named=ordinary_request;named.source_lut=ChannelDisplayLut{};named.source_lut->name="Green";
  named.output_title="Named projection";
  auto named_request=imagej_projection_request_from_record(imagej_projection_request_record(named));
  auto named_command=resolve_imagej_projection_command(ordinary.snapshot(),named_request);
  auto named_output=prepare_imagej_projection(named_command).prepared_new_document()->snapshot();
  check(named_output.data().metadata.title=="Named projection");
  check(recognized_channel_display(named_output.display(),named_output.data().dataset)->channels[0].lut.name=="Green");
  auto generated8=ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{-7,2},"unit",true);
  check(generated8.table_words()[0]==0 && generated8.table_words()[4]==0x3f800000U);
  auto generated16=ImageJIntensityCalibration::function(PixelType::UInt16,"straight_line",{-7,2},"unit",true);
  check(generated16.table_words()[0]==0xc0e00000U);
  auto nonlinear=ImageJIntensityCalibration::function(PixelType::UInt16,"poly2",{-32768,1,0.001});
  check(nonlinear.signed16_density());
  auto domain=ImageJIntensityCalibration::function(PixelType::UInt8,"log2",{2,3,10});
  check(domain.table_words()[0]==0x7fc00000U);
  reject([&]{ImageJIntensityCalibration::function(PixelType::Float32,"straight_line",{0,1});});
  reject([&]{ImageJIntensityCalibration::function(PixelType::UInt16,"uncalibrated_od",{});});
  reject([&]{ImageJIntensityCalibration::function(PixelType::UInt8,"poly2",{0,1});});
  auto fitted=ImageJIntensityCalibration::fit_polynomial(PixelType::UInt16,"straight_line",{0,1,2},{5,7,9},"unit");
  check(fitted.table_words()[0]==0x40a00000U && fitted.table_words()[1]==0x40e00000U);
  auto fitSigned=ImageJIntensityCalibration::fit_polynomial(PixelType::UInt16,"straight_line",{0,65535},{-32768,32767});
  check(fitSigned.signed16_density());
  auto quadratic=ImageJIntensityCalibration::fit_polynomial(PixelType::UInt8,"poly2",{0,1,2,3},{1,4,9,16});
  check(quadratic.table_words()[1]==0x40800000U);
  reject([&]{ImageJIntensityCalibration::fit_polynomial(PixelType::UInt8,"poly2",{1,1,1},{1,2,3});});
  reject([&]{ImageJIntensityCalibration::fit_polynomial(PixelType::UInt8,"straight_line",{0,256},{0,1});});
  reject([&]{ImageJIntensityCalibration::fit_polynomial(PixelType::UInt8,"straight_line",{0,1},{0});});
  const std::vector<std::pair<std::string,std::vector<double>>> models={
    {"exponential",{3,.08}},{"power",{2,.7}},{"log",{2,.3}},{"rodbard",{20,1.7,7,2}},
    {"gamma_variate",{1,2,2,3}},{"log2",{2,3,1}},{"rodbard2",{1,1.6,3,30}},
    {"exp_with_offset",{10,.2,2}},{"gaussian",{1,10,10,3}},{"exp_recovery",{10,.2,2}}};
  for(const auto& model:models) {
    std::cerr<<"Nonlinear fit "<<model.first<<"\n";
    const auto words=ImageJIntensityCalibration::function(PixelType::UInt16,model.first,model.second).table_words();
    const int offset=model.first=="power" || model.first=="log"?1:model.first=="log2" || model.first=="rodbard2"?2:0;
    std::vector<double> x,y;
    for(int i=0;i<21;++i){x.push_back(i+offset);float value;const auto word=words[std::size_t(i+offset)];std::memcpy(&value,&word,4);y.push_back(value);}
    const auto fit=ImageJIntensityCalibration::fit_nonlinear(PixelType::UInt16,model.first,x,y);
    const auto& diagnostic=std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(fit.source_parameters().at("fit"))->value);
    check(std::get<double>(diagnostic.at("rmse"))<1e-4);
  }
  reject([&]{ImageJIntensityCalibration::fit_nonlinear(PixelType::UInt8,"log",{0,1},{1,2});});
  reject([&]{ImageJIntensityCalibration::fit_nonlinear(PixelType::UInt8,"exponential",{0,1,2},{5,10,20},"unit",false,{4,.5},1);});
  auto fitOwner=source();Application fitApp;fitApp.adopt(fitOwner);const auto fitSource=fitOwner->snapshot();const auto fitHistory=fitOwner->undo_steps();
  ImageJIntensityFitRequest fitRequest;fitRequest.function="exponential";fitRequest.measured={0,1,2};fitRequest.known={5,10,20};fitRequest.initial_coefficients={4,.5};
  const auto fitBytes=estimate_imagej_intensity_fit(fitSource,fitRequest);check(fitBytes>0);
  auto smallFit=fitRequest;smallFit.max_working_bytes=fitBytes-1;reject([&]{estimate_imagej_intensity_fit(fitSource,smallFit);});
  bool fitStage=false;ExecutionControl fitControl;fitControl.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total){check(done<=total);if(stage=="Calibration nonlinear fit")fitStage=true;};
  auto fitPrepared=prepare_imagej_intensity_fit(fitSource,fitRequest,fitControl);
  check(fitStage && fitOwner->snapshot().shares_data(fitSource));check(!fitPrepared.prepared_new_document());
  auto fitPublished=fitApp.publish(fitPrepared);check(!fitPublished.created && fitPublished.document==fitOwner);
  check(fitOwner->undo_steps()==fitHistory+1 && fitOwner->snapshot().data().dataset.shares_pixels(fitSource.data().dataset));
  fitOwner->undo();check(metadata_equal(read_imagej_intensity_calibration(fitOwner->snapshot()).record(),read_imagej_intensity_calibration(fitSource).record()));fitOwner->redo();
  auto fitCancel=std::make_shared<CancellationToken>();ExecutionControl fitCancelled;fitCancelled.cancellation=fitCancel;
  fitCancelled.stage_progress=[&](std::string_view stage,std::size_t,std::size_t){if(stage=="Calibration nonlinear fit")fitCancel->cancel();};
  const auto beforeCancel=fitOwner->snapshot();reject<OperationCancelled>([&]{prepare_imagej_intensity_fit(beforeCancel,fitRequest,fitCancelled);});
  check(fitOwner->snapshot().shares_data(beforeCancel));
  auto fitStale=prepare_imagej_intensity_fit(beforeCancel,fitRequest);auto fitView=fitOwner->snapshot().display();fitView.zoom=2;fitOwner->set_display(fitView);
  reject<RevisionConflict>([&]{fitApp.publish(fitStale);});
  const auto beforeReadyCancel=fitOwner->snapshot();fitCancel=std::make_shared<CancellationToken>();fitCancelled={};fitCancelled.cancellation=fitCancel;
  auto fitReadyCancel=prepare_imagej_intensity_fit(beforeReadyCancel,fitRequest,fitCancelled);fitCancel->cancel();
  reject<OperationCancelled>([&]{fitApp.publish(fitReadyCancel);});check(fitOwner->snapshot().shares_data(beforeReadyCancel));
  std::cout<<"Prepared scientific projection: metadata, channel display, source/candidate guards, cancellation, session and capture passed\n";
} catch(const std::exception& error) {
  std::cerr<<error.what()<<"\n";return 1;
}
