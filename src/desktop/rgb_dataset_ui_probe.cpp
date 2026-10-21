#include "rgb_dataset_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/color_stack_command.hpp>
#include <imagejxx/indexed_dataset_command.hpp>
#include <imagejxx/indexed_rgb_command.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <chrono>
#include <thread>
namespace imagejxx::desktop {
Json rgbDatasetUiCase(Window &window,const std::string &context,const std::string &fresh) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh));
  if(fs::exists(directory)||!window.documents().empty())throw std::invalid_argument("Fresh RGB UI capture required");
  fs::create_directories(directory);Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){
    Image image(3,2,1,PixelType::UInt8);for(int y=0;y<2;++y)for(int x=0;x<3;++x)image.at(x,y)=10+100*t+20*c+z+3*y+x;
    planes.push_back(std::move(image));
  }
  const DatasetShape shape{2,2,2,2,3,1};auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes(shape,std::move(planes)));
  auto display=owner->snapshot().display();display.t=display.c=1;display.crosshair={1,1,1};
  ChannelDisplayState channels;channels.channels.resize(2);channels.channels[0].lut.name="Red";channels.channels[1].lut.name="Green";
  display.properties["channel_display"]=metadata_object(encode_channel_display(channels,shape,PixelType::UInt8,FloatingSamplePolicy::FiniteOnly));owner->set_display(display);
  const auto workflow_source=owner->snapshot();
  window.addNativeDocument(owner);window.recording=true;
  Json report{{"context",context},{"frames",Json::array()},{"controls",Json::array()}};HeadlessInputSession session(window);Json last;
  auto frame=[&](Json events=Json::array()){
    if(report["frames"].size()>800)throw std::runtime_error("RGB capture frame cap");
    last=session.frame(events);if(last.at("input_frame").at("raw_inputs")!=events)throw std::runtime_error("RGB input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto click=[&](Json control,const char *name){
    if(!control.at("enabled").get<bool>()||control.at("id")==0)throw std::runtime_error("RGB control unavailable");
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto keep_program=[&](){
    click(last.at("native_workflow").at("controls").at("prepare_program"),"Prepare edited color program");window.wait();frame();
    click(last.at("native_workflow").at("controls").at("keep_program"),"Keep edited color program");frame();
    window.action("run_workflow");frame();frame();
    click(last.at("native_workflow").at("controls").at("confirm"),"Confirm retained color program");
  };
  try {
  frame();frame();click(last.at("host_controls").at("menu.Image"),"Image");
  click(last.at("host_controls").at("menu.Image.Type"),"Type");
  click(last.at("host_controls").at("menu.rgb_dataset"),"RGB menu");
  click(last.at("rgb_dataset").at("controls").at("mode"),"Rendering");
  click(last.at("rgb_dataset").at("controls").at("mode.composite"),"Composite");
  click(last.at("rgb_dataset").at("controls").at("replace"),"Replace");
  click(last.at("rgb_dataset").at("controls").at("preview"),"Preview");
  window.wait();frame();report["preview"]=last.at("rgb_dataset");report["history_before_apply"]=owner->undo_steps();report["workflow_before_apply"]=window.workflow;
  click(last.at("rgb_dataset").at("controls").at("apply"),"Apply");window.wait();frame();
  const auto result=owner->snapshot();report["shape"]=result.data().dataset.shape().dimensions();report["pixels"]=Json::array();
  for(int t=0;t<2;++t)for(int z=0;z<2;++z)report["pixels"].push_back(result.data().dataset.plane({t,0,z})->pixels);
  report["workflow"]=window.workflow;report["undo_steps"]=owner->undo_steps();report["display"]=metadataJson(result.display().properties);
  Image scalar(3,1,1,PixelType::UInt16);scalar.pixels={0,128,255};
  auto legacy=std::make_shared<imagejxx::Document>(Dataset::from_planes({1,1,1,1,3,1},Stack{scalar}));
  auto legacy_view=legacy->snapshot().display();legacy_view.properties["lut"]=std::string("Red");legacy->set_display(legacy_view);
  window.addNativeDocument(legacy);frame();frame();
  click(last.at("host_controls").at("menu.Image"),"Legacy Image");
  click(last.at("host_controls").at("menu.Image.Type"),"Legacy Type");
  click(last.at("host_controls").at("menu.rgb_dataset"),"Legacy RGB menu");
  click(last.at("rgb_dataset").at("controls").at("mode"),"Legacy Rendering");
  click(last.at("rgb_dataset").at("controls").at("mode.composite"),"Legacy Composite");
  const auto legacy_record=last.at("rgb_dataset").at("record");
  click(last.at("rgb_dataset").at("controls").at("apply"),"Legacy Apply");window.wait();frame();
  const auto legacy_result=window.nativeSnapshot();
  report["deferred_legacy"]={{"shape",legacy_result.data().dataset.shape().dimensions()},
      {"pixels",legacy_result.data().dataset.plane({0,0,0})->pixels},{"record",legacy_record},{"source_undo_steps",legacy->undo_steps()}};
  auto mixed_owner=std::make_shared<imagejxx::Document>(workflow_source);window.addNativeDocument(mixed_owner);
  NativeWorkflowOperation first;first.operation="add";first.parameters={{"value",1.}};
  first.target.scope=TargetScope::Ranges;first.target.roi=RoiPolicy::WholePlane;first.target.t=first.target.c=first.target.z=AxisRange{0,2,1};
  NativeWorkflowOperation rgb;rgb.operation="rgb_dataset";rgb.original=metadataObjectFromJson(report["workflow"][0]);rgb.target=first.target;
  auto last_step=first;last_step.parameters={{"value",2.}};last_step.target.c=AxisRange{0,1,1};
  const auto mixed_program=metadataJson(make_native_workflow_program({first,rgb,last_step}).record());
  window.workflow=Json::array({mixed_program});window.action("run_workflow");frame();frame();
  for(int i=0;i<3;++i){const auto key="step."+std::to_string(i);click(last.at("native_workflow").at("controls").at(key),"Workflow step");click(last.at("native_workflow").at("controls").at("confirm"),"Confirm workflow step");}
  click(last.at("native_workflow").at("controls").at("prepare"),"Prepare mixed RGB");window.wait();frame();
  report["mixed_workflow"]={{"review",last.at("native_workflow")},{"history_before_apply",mixed_owner->undo_steps()}};
  click(last.at("native_workflow").at("controls").at("apply"),"Apply mixed RGB");window.wait();frame();
  const auto mixed_result=mixed_owner->snapshot();report["mixed_workflow"]["shape"]=mixed_result.data().dataset.shape().dimensions();
  report["mixed_workflow"]["pixels"]=Json::array();
  for(int t=0;t<2;++t)for(int z=0;z<2;++z)report["mixed_workflow"]["pixels"].push_back(mixed_result.data().dataset.plane({t,0,z})->pixels);
  report["mixed_workflow"]["undo_steps"]=mixed_owner->undo_steps();report["mixed_workflow"]["workflow"]=window.workflow;
  report["mixed_workflow"]["program"]=mixed_program;
  click(last.at("native_workflow").at("controls").at("cancel"),"Close mixed RGB review");
  mixed_owner->undo();report["mixed_workflow"]["undo_shape"]=mixed_owner->snapshot().data().dataset.shape().dimensions();
  mixed_owner->redo();report["mixed_workflow"]["redo_shape"]=mixed_owner->snapshot().data().dataset.shape().dimensions();
  Stack color_planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){
    Image image(3,2,3,PixelType::UInt8);for(int i=0;i<6;++i)for(int k=0;k<3;++k)image.pixels[3*i+k]=10+50*t+20*c+z+i+20*k;
    color_planes.push_back(std::move(image));
  }
  SpatialCalibration color_cal;color_cal.spacing={2,3,5};color_cal.unit="um";
  auto color_owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({2,2,2,2,3,3},std::move(color_planes),color_cal));
  PlanarDisplayAnnotation layer;layer.name="XZ label";layer.plane=Plane::XZ;layer.coordinate=0;layer.t=layer.c=1;layer.payload=TextAnnotationPayload{};
  auto layer_edit=prepare_store_planar_display_annotation(color_owner->snapshot(),layer);color_owner->commit(layer_edit);
  color_owner=std::make_shared<imagejxx::Document>(color_owner->snapshot());
  auto color_view=color_owner->snapshot().display();color_view.t=color_view.c=1;color_view.crosshair={1,1,1};color_view.orthogonal=true;color_owner->set_display(color_view);
  const auto color_source=color_owner->snapshot();
  const auto original_color=metadataJson(resolve_color_stack_command(color_owner->snapshot()).record());
  window.addNativeDocument(color_owner);window.workflow=Json::array({original_color});window.action("run_workflow");frame();frame();
  click(last.at("native_workflow").at("controls").at("edit_parameters"),"Edit color-stack choices");window.wait();frame();
  report["color_stack_workflow"]={{"original",original_color},{"editor_before",last.at("native_workflow").at("editor")}};
  click(last.at("native_workflow").at("editor").at("controls").at("space"),"Component space");
  click(last.at("native_workflow").at("editor").at("controls").at("space.choice.2"),"HSB 32-bit components");
  click(last.at("native_workflow").at("editor").at("controls").at("keep_parameters"),"Keep color-stack choices");frame();
  click(last.at("native_workflow").at("controls").at("confirm"),"Confirm color-stack step");
  keep_program();
  click(last.at("native_workflow").at("controls").at("prepare"),"Prepare color-stack workflow");window.wait();frame();
  report["color_stack_workflow"]["review"]=last.at("native_workflow");report["color_stack_workflow"]["history_before_apply"]=color_owner->undo_steps();
  click(last.at("native_workflow").at("controls").at("apply"),"Apply color-stack workflow");window.wait();frame();
  const auto color_result=color_owner->snapshot();auto &color_report=report["color_stack_workflow"];
  color_report["shape"]=color_result.data().dataset.shape().dimensions();color_report["pixels"]=Json::array();
  for(int t=0;t<2;++t)for(int c=0;c<6;++c)for(int z=0;z<2;++z)color_report["pixels"].push_back(color_result.data().dataset.plane({t,c,z})->pixels);
  color_report["workflow"]=window.workflow;color_report["undo_steps"]=color_owner->undo_steps();color_report["spacing"]=color_result.data().dataset.calibration().spacing;
  color_report["view"]={{"t",color_result.display().t},{"c",color_result.display().c},{"crosshair",color_result.display().crosshair}};
  color_report["layers"]=planar_display_annotations(color_result.data()).size();
  click(last.at("native_workflow").at("controls").at("cancel"),"Close color-stack review");
  color_owner->undo();color_report["undo_shape"]=color_owner->snapshot().data().dataset.shape().dimensions();
  color_owner->redo();color_report["redo_shape"]=color_owner->snapshot().data().dataset.shape().dimensions();
  auto legacy_color=std::make_shared<imagejxx::Document>(color_source);window.addNativeDocument(legacy_color);
  window.workflow=Json::array({original_color});window.action("run_workflow");frame();frame();
  click(last.at("native_workflow").at("controls").at("edit_parameters"),"Edit legacy color choices");window.wait();frame();
  auto choice=[&](const char *name,const char *value){click(last.at("native_workflow").at("editor").at("controls").at(name),name);click(last.at("native_workflow").at("editor").at("controls").at(value),value);};
  choice("space","space.choice.2");choice("layout","layout.choice.1");
  click(last.at("native_workflow").at("editor").at("controls").at("allow_calibration_reset"),"Allow calibration reset");
  click(last.at("native_workflow").at("editor").at("controls").at("allow_nonxy_annotation_drop"),"Allow XZ/YZ removal");
  click(last.at("native_workflow").at("editor").at("controls").at("keep_parameters"),"Keep legacy color choices");frame();
  click(last.at("native_workflow").at("controls").at("confirm"),"Confirm legacy color step");
  keep_program();
  click(last.at("native_workflow").at("controls").at("prepare"),"Prepare legacy color workflow");window.wait();frame();
  report["legacy_color_workflow"]={{"review",last.at("native_workflow")},{"history_before_apply",legacy_color->undo_steps()}};
  click(last.at("native_workflow").at("controls").at("apply"),"Apply legacy color workflow");window.wait();frame();
  const auto legacy_color_result=legacy_color->snapshot();auto &legacy_report=report["legacy_color_workflow"];
  legacy_report["shape"]=legacy_color_result.data().dataset.shape().dimensions();legacy_report["pixels"]=Json::array();
  for(int c=0;c<3;++c)for(int z=0;z<8;++z)legacy_report["pixels"].push_back(legacy_color_result.data().dataset.plane({0,c,z})->pixels);
  legacy_report["workflow"]=window.workflow;legacy_report["unit"]=legacy_color_result.data().dataset.calibration().unit;
  legacy_report["layers"]=planar_display_annotations(legacy_color_result.data()).size();legacy_report["undo_steps"]=legacy_color->undo_steps();
  // The standalone menu uses the same native command, with worker preview and recording.
  click(last.at("native_workflow").at("controls").at("cancel"),"Close legacy workflow review");
  auto standalone=std::make_shared<imagejxx::Document>(color_source);window.addNativeDocument(standalone);frame();frame();
  auto &standalone_report=report["standalone_color_stack"];
  standalone_report["source_shape"]=standalone->snapshot().data().dataset.shape().dimensions();
  click(last.at("host_controls").at("menu.Image"),"Component Image");
  click(last.at("host_controls").at("menu.Image.Type"),"Component Type");
  click(last.at("host_controls").at("menu.color_stack"),"Component menu");
  click(last.at("color_stack").at("controls").at("space"),"Component space");
  click(last.at("color_stack").at("controls").at("space.hsb32"),"HSB32 components");
  click(last.at("color_stack").at("controls").at("layout"),"Component layout");
  click(last.at("color_stack").at("controls").at("layout.imagej_legacy"),"Legacy component layout");
  standalone_report["missing_consent"]=last.at("color_stack");
  click(last.at("color_stack").at("controls").at("allow_calibration_reset"),"Reset calibration consent");
  click(last.at("color_stack").at("controls").at("allow_nonxy_annotation_drop"),"Remove nonXY consent");
  click(last.at("color_stack").at("controls").at("replace"),"Replace component source");
  click(last.at("color_stack").at("controls").at("preview"),"Preview components");window.wait();frame();
  standalone_report["preview"]=last.at("color_stack");standalone_report["history_before_apply"]=standalone->undo_steps();
  click(last.at("color_stack").at("controls").at("apply"),"Apply components");window.wait();frame();
  standalone_report["record"]=window.workflow.back();
  const auto standalone_result=standalone->snapshot();standalone_report["shape"]=standalone_result.data().dataset.shape().dimensions();
  standalone_report["unit"]=standalone_result.data().dataset.calibration().unit;standalone_report["layers"]=planar_display_annotations(standalone_result.data()).size();
  standalone_report["undo_steps"]=standalone->undo_steps();standalone_report["pixels"]=Json::array();
  for(int t=0;t<standalone_result.data().dataset.shape().t;++t)for(int c=0;c<standalone_result.data().dataset.shape().c;++c)for(int z=0;z<standalone_result.data().dataset.shape().z;++z)
    standalone_report["pixels"].push_back(standalone_result.data().dataset.plane({t,c,z})->pixels);
  standalone->undo();standalone_report["undo_shape"]=standalone->snapshot().data().dataset.shape().dimensions();standalone_report["undo_unit"]=standalone->snapshot().data().dataset.calibration().unit;
  standalone_report["undo_layers"]=planar_display_annotations(standalone->snapshot().data()).size();standalone->redo();standalone_report["redo_shape"]=standalone->snapshot().data().dataset.shape().dimensions();
  Stack scalar_planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){Image image(3,2,1,PixelType::UInt16);for(int i=0;i<6;++i)image.pixels[i]=100*(t*4+z*2+c)+1000*i;scalar_planes.push_back(std::move(image));}
  auto scalar_owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({2,2,2,2,3,1},std::move(scalar_planes),color_cal));
  auto scalar_view=scalar_owner->snapshot().display();scalar_view.t=scalar_view.c=1;scalar_view.crosshair={1,1,1};scalar_view.properties=metadataObjectFromJson({{"display_range",{100,6000}},{"lut","Red"}});scalar_owner->set_display(scalar_view);
  window.addNativeDocument(scalar_owner);frame();frame();auto &scalar_report=report["scalar_rgb_flatten"];
  click(last.at("host_controls").at("menu.Image"),"Scalar component Image");click(last.at("host_controls").at("menu.Image.Type"),"Scalar component Type");click(last.at("host_controls").at("menu.color_stack"),"Scalar component menu");
  click(last.at("color_stack").at("controls").at("action"),"Scalar action");click(last.at("color_stack").at("controls").at("action.flatten_rgb"),"Render and flatten scalar RGB");
  click(last.at("color_stack").at("controls").at("layout"),"Scalar layout");click(last.at("color_stack").at("controls").at("layout.imagej_legacy"),"Scalar legacy layout");
  click(last.at("color_stack").at("controls").at("allow_calibration_reset"),"Scalar calibration consent");click(last.at("color_stack").at("controls").at("replace"),"Scalar replacement");
  click(last.at("color_stack").at("controls").at("preview"),"Scalar RGB preview");window.wait();frame();scalar_report["preview"]=last.at("color_stack");scalar_report["history_before_apply"]=scalar_owner->undo_steps();
  click(last.at("color_stack").at("controls").at("apply"),"Scalar RGB Apply");window.wait();frame();scalar_report["record"]=window.workflow.back();scalar_report["shape"]=scalar_owner->snapshot().data().dataset.shape().dimensions();scalar_report["pixels"]=Json::array();
  for(int z=0;z<8;++z)scalar_report["pixels"].push_back(scalar_owner->snapshot().data().dataset.plane({0,0,z})->pixels);
  scalar_report["undo_steps"]=scalar_owner->undo_steps();scalar_owner->undo();scalar_report["undo_shape"]=scalar_owner->snapshot().data().dataset.shape().dimensions();scalar_report["undo_unit"]=scalar_owner->snapshot().data().dataset.calibration().unit;scalar_owner->redo();scalar_report["redo_shape"]=scalar_owner->snapshot().data().dataset.shape().dimensions();
  Image indexed_image(2,1,3,PixelType::UInt8);indexed_image.pixels={255,0,0,0,255,0};
  auto indexed_owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({1,1,1,1,2,3},Stack{indexed_image}));
  const auto quantize_command=resolve_indexed_dataset_command(indexed_owner->snapshot());
  const auto indexed_preview=prepare_indexed_dataset(quantize_command).preview();
  const auto expand_command=resolve_indexed_rgb_command(indexed_preview);
  NativeWorkflowOperation quantize;quantize.operation="indexed_dataset";quantize.original=quantize_command.record();
  quantize.target.scope=TargetScope::Ranges;quantize.target.roi=RoiPolicy::WholePlane;
  quantize.target.t=quantize.target.c=quantize.target.z=AxisRange{0,1,1};
  auto expand=quantize;expand.operation="indexed_rgb";expand.original=expand_command.record();
  window.addNativeDocument(indexed_owner);window.workflow=Json::array({metadataJson(make_native_workflow_program({quantize,expand}).record())});
  window.action("run_workflow");frame();frame();
  auto &indexed_report=report["indexed_workflow"];
  indexed_report["quantize_original"]=metadataJson(quantize_command.record());indexed_report["expand_original"]=metadataJson(expand_command.record());
  click(last.at("native_workflow").at("controls").at("edit_parameters"),"Edit indexed colors");window.wait();frame();
  click(last.at("native_workflow").at("editor").at("controls").at("max_colors"),"Indexed color count");
  for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
  frame(Json::array({{{"type","text"},{"value","2"}}}));frame();
  click(last.at("native_workflow").at("editor").at("controls").at("keep_parameters"),"Keep indexed colors");frame();
  click(last.at("native_workflow").at("controls").at("confirm"),"Confirm indexed colors");
  click(last.at("native_workflow").at("controls").at("step.1"),"Select indexed RGB step");
  click(last.at("native_workflow").at("controls").at("edit_parameters"),"Edit indexed palette source");window.wait();frame();
  click(last.at("native_workflow").at("editor").at("controls").at("palette_source"),"Indexed palette source");
  click(last.at("native_workflow").at("editor").at("controls").at("palette_source.choice.1"),"Stored indexed palette");
  click(last.at("native_workflow").at("editor").at("controls").at("keep_parameters"),"Keep indexed palette source");frame();
  click(last.at("native_workflow").at("controls").at("confirm"),"Confirm indexed RGB");
  click(last.at("native_workflow").at("controls").at("prepare_program"),"Prepare indexed program");window.wait();frame();
  click(last.at("native_workflow").at("controls").at("keep_program"),"Keep indexed program");frame();
  window.action("run_workflow");frame();frame();
  for(int i=0;i<2;++i){click(last.at("native_workflow").at("controls").at("step."+std::to_string(i)),"Select retained indexed step");click(last.at("native_workflow").at("controls").at("confirm"),"Confirm retained indexed step");}
  click(last.at("native_workflow").at("controls").at("prepare"),"Prepare indexed workflow");window.wait();frame();
  indexed_report["review"]=last.at("native_workflow");indexed_report["history_before_apply"]=indexed_owner->undo_steps();
  click(last.at("native_workflow").at("controls").at("apply"),"Apply indexed workflow");window.wait();frame();
  const auto indexed_result=indexed_owner->snapshot();indexed_report["shape"]=indexed_result.data().dataset.shape().dimensions();indexed_report["pixels"]=indexed_result.data().dataset.plane({0,0,0})->pixels;
  indexed_report["palette"]=metadataJson(encode_indexed_palette(*indexed_palette(indexed_result.data())));indexed_report["workflow"]=window.workflow;indexed_report["undo_steps"]=indexed_owner->undo_steps();
  click(last.at("native_workflow").at("controls").at("cancel"),"Close indexed review");
  indexed_owner->undo();indexed_report["undo_shape"]=indexed_owner->snapshot().data().dataset.shape().dimensions();indexed_owner->redo();indexed_report["redo_shape"]=indexed_owner->snapshot().data().dataset.shape().dimensions();
  auto standalone_indexed=std::make_shared<imagejxx::Document>(Dataset::from_planes({1,1,1,1,2,3},Stack{indexed_image}));
  const auto standalone_source=standalone_indexed->snapshot();window.addNativeDocument(standalone_indexed);frame();frame();
  click(last.at("host_controls").at("menu.Image"),"Indexed Image");click(last.at("host_controls").at("menu.Image.Type"),"Indexed Type");click(last.at("host_controls").at("menu.indexed_dataset"),"Indexed menu");
  click(last.at("indexed_color").at("controls").at("max_colors"),"Standalone maximum colors");
  for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
  frame(Json::array({{{"type","text"},{"value","2"}}}));frame();
  click(last.at("indexed_color").at("controls").at("replace"),"Replace indexed source");
  click(last.at("indexed_color").at("controls").at("preview"),"Preview indexed palette");window.wait();frame();
  auto &standalone_indexed_report=report["standalone_indexed"];
  standalone_indexed_report["preview"]=last.at("indexed_color");standalone_indexed_report["history_before_apply"]=standalone_indexed->undo_steps();
  click(last.at("indexed_color").at("controls").at("apply"),"Apply indexed palette");window.wait();frame();
  standalone_indexed_report["record"]=window.workflow.back();standalone_indexed_report["shape"]=standalone_indexed->snapshot().data().dataset.shape().dimensions();standalone_indexed_report["undo_steps"]=standalone_indexed->undo_steps();
  standalone_indexed->undo();standalone_indexed_report["undo_shape"]=standalone_indexed->snapshot().data().dataset.shape().dimensions();standalone_indexed->redo();standalone_indexed_report["redo_shape"]=standalone_indexed->snapshot().data().dataset.shape().dimensions();
  click(last.at("host_controls").at("menu.Image"),"Indexed RGB Image");click(last.at("host_controls").at("menu.Image.Type"),"Indexed RGB Type");click(last.at("host_controls").at("menu.indexed_rgb"),"Indexed RGB menu");
  click(last.at("indexed_color").at("controls").at("palette_source"),"Indexed RGB palette source");click(last.at("indexed_color").at("controls").at("palette_source.stored"),"Stored palette expansion");
  click(last.at("indexed_color").at("controls").at("replace"),"Replace indices with RGB");click(last.at("indexed_color").at("controls").at("preview"),"Preview indexed RGB");window.wait();frame();
  auto &standalone_expansion=report["standalone_indexed_rgb"];standalone_expansion["preview"]=last.at("indexed_color");
  click(last.at("indexed_color").at("controls").at("apply"),"Apply indexed RGB");window.wait();frame();
  standalone_expansion["record"]=window.workflow.back();standalone_expansion["shape"]=standalone_indexed->snapshot().data().dataset.shape().dimensions();standalone_expansion["pixels"]=standalone_indexed->snapshot().data().dataset.plane({0,0,0})->pixels;standalone_expansion["undo_steps"]=standalone_indexed->undo_steps();
  auto replay_owner=std::make_shared<imagejxx::Document>(standalone_source);window.addNativeDocument(replay_owner);
  frame();frame();click(last.at("host_controls").at("workflow.indexed_color."+std::to_string(window.workflow.size()-2)),"Review recorded indexed color");frame();frame();standalone_indexed_report["replay"]=last.at("indexed_color");standalone_indexed_report["replay_owner"]=replay_owner->id;
  click(last.at("indexed_color").at("controls").at("cancel"),"Cancel restored indexed capture");
  standalone_indexed->undo();auto type_owner=std::make_shared<imagejxx::Document>(standalone_indexed->snapshot());window.addNativeDocument(type_owner);frame();frame();
  click(last.at("host_controls").at("menu.Image"),"Indexed grayscale Image");click(last.at("host_controls").at("menu.Image.Type"),"Indexed grayscale Type");click(last.at("host_controls").at("menu.command.convert_image"),"Indexed grayscale conversion");
  click(last.at("dialog").at("controls").at("apply"),"Apply indexed grayscale8");window.wait();frame();
  auto &type_report=report["indexed_type_conversion"];type_report["gray_pixels"]=type_owner->snapshot().data().dataset.plane({0,0,0})->pixels;type_report["gray_palette"]=metadataJson(encode_indexed_palette(*indexed_palette(type_owner->snapshot().data())));
  click(last.at("host_controls").at("menu.Image"),"Promoted indexed Image");click(last.at("host_controls").at("menu.Image.Type"),"Promoted indexed Type");click(last.at("host_controls").at("menu.command.convert_image"),"Promoted indexed conversion");
  click(last.at("dialog").at("controls").at("dtype"),"Indexed output dtype");click(last.at("dialog").at("controls").at("dtype.choice.float32"),"Indexed float32");
  click(last.at("dialog").at("controls").at("apply"),"Apply indexed float32");window.wait();frame();
  type_report["pixels"]=type_owner->snapshot().data().dataset.plane({0,0,0})->pixels;type_report["type"]=int(type_owner->snapshot().data().dataset.pixel_type());type_report["undo_steps"]=type_owner->undo_steps();type_report["record"]=window.workflow.back();
  type_owner->undo();type_owner->undo();type_report["undo_palette"]=metadataJson(encode_indexed_palette(*indexed_palette(type_owner->snapshot().data())));type_owner->redo();type_owner->redo();type_report["redo_pixels"]=type_owner->snapshot().data().dataset.plane({0,0,0})->pixels;
  session.close();legacy_color->undo();legacy_report["undo_unit"]=legacy_color->snapshot().data().dataset.calibration().unit;
  legacy_report["undo_layers"]=planar_display_annotations(legacy_color->snapshot().data()).size();legacy_color->redo();legacy_report["redo_shape"]=legacy_color->snapshot().data().dataset.shape().dimensions();
  owner->undo();report["undo_shape"]=owner->snapshot().data().dataset.shape().dimensions();
  owner->redo();report["redo_shape"]=owner->snapshot().data().dataset.shape().dimensions();report["error"]=window.error();
  if(report.dump().size()>64*1024*1024)throw std::runtime_error("RGB report byte cap");
  writeJson((directory/"case.json").u8string(),report);return report;
  }catch(const std::exception &e){report["failure"]=e.what();report["last"]=last;writeJson((directory/"failure.json").u8string(),report);throw;}
}
}
