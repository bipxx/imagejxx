#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/native_workflow.hpp>
#include <imagejxx/contrast_apply_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "../../src/native/contrast_apply_workflow.hpp"
#include "../../src/native/native_workflow_internal.hpp"
#include "../../src/native/axis_command_record.hpp"
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/rgb_dataset_command.hpp>
#include <imagejxx/color_stack_command.hpp>
#include <imagejxx/indexed_dataset_command.hpp>
#include <imagejxx/indexed_rgb_command.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool b,int line) {
  if (!b)
    throw std::runtime_error("Native workflow assertion at line "+std::to_string(line));
}
#define check(...) check((__VA_ARGS__),__LINE__)
template <class F> void refuses(F f) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("Expected workflow refusal");
}
void contrast_workflow_contract() {
  Image image(4,1,1,PixelType::UInt16);image.pixels={10,20,30,40};DatasetReadObserver reads;
  Document owner(Dataset::from_stack({image}).observe_reads(reads));
  auto view=owner.snapshot().display();ManualThresholdState threshold;threshold.range={15,35};
  set_manual_threshold_state(view,owner.snapshot().data().dataset.shape(),threshold);
  view.properties["display_range"]=metadata_array({10.,40.});owner.set_display(view);
  const auto source=owner.snapshot();TargetRequest target;target.roi=RoiPolicy::WholePlane;
  NativeWorkflowOperation apply;apply.operation="contrast_apply";apply.target=target;
  apply.original=resolve_contrast_apply_command(source,{10,40,12},target).record();
  const auto saved=*apply.original;
  apply.parameters={{"minimum",10.},{"maximum",40.},{"unsigned16_bits",std::int64_t(0)}};
  const auto program=native_workflow_program_from_json(make_native_workflow_program({apply}).json());
  check(metadata_equal(*std::get<NativeWorkflowOperation>(program.steps()[0]).original,saved));
  check(reads.counts().plane_reads==0);
  (void)detail::admit_native_workflow(source,{&apply},{});
  const auto command=detail::workflow_contrast_apply(source,apply,{});
  const auto descriptor=detail::contrast_apply_metadata_preview(command);
  check(!manual_threshold_state(descriptor.display(),descriptor.data().dataset.shape()));
  check(reads.counts().plane_reads==0);refuses([&]{descriptor.data().dataset.plane();});
  check(reads.counts().plane_reads==0);
  NativeWorkflowOperation bad=apply;bad.parameters["unsigned16_bits"]=true;
  refuses([&]{prepare_native_workflow(source,{apply,bad});});
  check(reads.counts().plane_reads==0);
  auto malformed=saved;malformed["future"]=true;bad=apply;bad.original=malformed;
  refuses([&]{make_native_workflow_program({bad});});check(reads.counts().plane_reads==0);
  NativeWorkflowHistogram histogram;histogram.name="after.csv";histogram.target=target;
  histogram.authored_options=NativeWorkflowHistogramOptions{};
  auto prepared=prepare_native_workflow(source,{program.steps()[0],histogram});
  check(prepared.completed_steps()==2 && prepared.histograms().size()==1 && prepared.histograms()[0].value.sum==131070);
  check(prepared.preview().data().dataset.plane()->pixels==std::vector<double>({0,21845,43690,65535}));
  check(prepared.dependencies()==Change::All && owner.snapshot().shares_data(source) && owner.undo_steps()==0);
  auto edit=prepared.begin_edit();owner.commit(edit);check(owner.undo_steps()==1);
  check(!manual_threshold_state(owner.snapshot().display(),source.data().dataset.shape()));
  owner.undo();check(owner.snapshot().shares_data(source) && manual_threshold_state(owner.snapshot().display(),source.data().dataset.shape()).has_value());
  owner.redo();check(owner.snapshot().data().dataset.plane()->pixels==std::vector<double>({0,21845,43690,65535}));owner.undo();
  owner.select(Selection::from_packed_mask(4,1,{0x40}));const auto masked=owner.snapshot();
  apply.target.roi=RoiPolicy::ActiveSelection;
  prepared=prepare_native_workflow(masked,{apply});
  check(prepared.preview().data().dataset.plane()->pixels==std::vector<double>({10,21845,30,40}));
  const auto &range=detail::axis_command_record::array_value(prepared.preview().display().properties.at("display_range"));
  check(detail::axis_command_record::number_value(range[0])==10 && detail::axis_command_record::number_value(range[1])==21845);
  auto disabled=apply;disabled.enabled=false;disabled.target.scope=TargetScope::Positions;disabled.target.positions={{0,0,999}};
  check(prepare_native_workflow(masked,{disabled}).completed_steps()==0);
  auto token=std::make_shared<CancellationToken>();ExecutionControl control;control.cancellation=token;
  control.stage_progress=[&](const auto &phase,auto,auto){if(phase=="Contrast Apply pixels")token->cancel();};
  refuses([&]{prepare_native_workflow(masked,{apply},{},control);});
  check(owner.snapshot().shares_data(masked));
}
void histogram_workflow_contract() {
  Image image(3,2,1,PixelType::UInt16);
  image.pixels={0,1,2,3,4,5};
  DatasetReadObserver reads;
  Document owner(Dataset::from_planes({1,1,1,2,3,1},{image}).observe_reads(reads));
  const auto source=owner.snapshot();
  TargetRequest target; target.roi=RoiPolicy::WholePlane;
  TargetHistogramOptions options; options.binning=HistogramBinning::UniformRange;
  options.bins=4; options.range=std::array<double,2>{0,12};
  NativeWorkflowHistogram first; first.name="before.csv";first.target=target;
  first.original=resolve_histogram_command(source,options,target,true).record();
  auto last=first;last.name="after.csv";
  NativeWorkflowOperation add;add.operation="add";add.parameters={{"value",6.}};add.target=target;
  const auto program=native_workflow_program_from_json(make_native_workflow_program({first,add,last}).json());
  check(reads.counts().plane_reads==0);
  auto invalid=last;invalid.limits.processing.max_pixels=1;
  refuses([&]{prepare_native_workflow(source,{add,invalid});});
  check(reads.counts().plane_reads==0);
  auto prepared=prepare_native_workflow(source,program.steps());
  check(prepared.completed_steps()==3 && prepared.histograms().size()==2 && prepared.outputs().empty());
  const auto &a=prepared.histograms()[0], &b=prepared.histograms()[1];
  check(a.step_index==0 && b.step_index==2 && a.value.sum==15 && b.value.sum==51);
  check(a.value.histogram.counts==std::vector<std::uint64_t>({3,3,0,0}));
  check(b.value.histogram.counts==std::vector<std::uint64_t>({0,0,3,3}));
  check(owner.snapshot().shares_data(source) && owner.history().undo_steps==0);
  auto edit=prepared.begin_edit();owner.commit(edit);
  check(owner.history().undo_steps==1);owner.undo();check(owner.snapshot().shares_data(source));
  auto only=prepare_native_workflow(owner.snapshot(),{first});auto noop=only.begin_edit();owner.commit(noop);
  check(owner.history().undo_steps==0 && only.histograms().size()==1);
  auto disabled=first;disabled.enabled=false;disabled.target.scope=TargetScope::Positions;
  disabled.target.positions={{0,0,999}};
  auto skipped=prepare_native_workflow(source,{disabled});
  check(skipped.histograms().empty() && skipped.completed_steps()==0);
  ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();
  control.progress=[&](std::size_t done,std::size_t total){if(done==total)control.cancellation->cancel();};
  refuses([&]{prepare_native_workflow(source,{first}, {},control);});
  auto authored=first;NativeWorkflowHistogramOptions choices;
  choices.histogram.binning=HistogramBinning::UniformRange;choices.histogram.bins=6;
  choices.histogram.range=std::array<double,2>{0,6};choices.log_counts=false;
  authored.authored_options=choices;
  const auto v5=make_native_workflow_program({authored,last});
  check(v5.json().find("\"version\":5")!=std::string::npos);
  const auto decoded=native_workflow_program_from_json(v5.json());
  check(metadata_equal(*std::get<NativeWorkflowHistogram>(decoded.steps()[0]).original,*first.original));
  const auto changed=prepare_native_workflow(source,decoded.steps());
  check(changed.histograms()[0].value.histogram.counts==std::vector<std::uint64_t>({1,1,1,1,1,1}));
  check(changed.histograms()[1].value.histogram.counts==std::vector<std::uint64_t>({3,3,0,0}));
  authored.original.reset();const auto fresh=make_native_workflow_program({authored});
  check(prepare_native_workflow(source,fresh.steps()).histograms()[0].value.sum==15);
  const auto old_reads=reads.counts().plane_reads;
  authored.authored_options->histogram.bins=0;authored.enabled=false;
  refuses([&]{make_native_workflow_program({authored});});
  refuses([&]{prepare_native_workflow(source,{add,authored});});
  check(reads.counts().plane_reads==old_reads);
}
void conversion_contract() {
  const DatasetShape shape{2, 2, 2, 2, 3, 1};
  Stack planes;
  for (int i = 0; i < 8; ++i) {
    Image image(3, 2, 1, PixelType::UInt16);
    for (int j = 0; j < 6; ++j) image.pixels[j] = 100 * i + j;
    planes.push_back(std::move(image));
  }
  DatasetReadObserver reads;
  Document owner(Dataset::from_planes(shape, planes).observe_reads(reads));
  auto display = owner.snapshot().display();
  display.properties["display_range"] = metadata_array({0., 4095.});
  owner.set_display(display);
  NativeWorkflowOperation op;
  op.operation = "convert_image";
  op.parameters = {{"dtype", std::string("uint8")}, {"scale", false}};
  op.target.scope = TargetScope::Ranges;
  op.target.roi = RoiPolicy::WholePlane;
  op.target.t = op.target.c = op.target.z = AxisRange{0, 2, 1};
  const auto source = owner.snapshot();
  auto partial = op;
  partial.target.z = AxisRange{0, 1, 1};
  refuses([&] { prepare_native_workflow(source, {partial}); });
  refuses([&] { prepare_native_workflow(source, {op}, {64, 64}); });
  auto unsupported = op;
  unsupported.operation = "grayscale";
  unsupported.parameters.clear();
  refuses([&] { make_native_workflow_program({unsupported}); });
  check(reads.counts().plane_reads == 0);
  const auto program = native_workflow_program_from_record(
      make_native_workflow_program({op}).record());
  auto plan = prepare_native_workflow(source, program.steps());
  check(plan.dependencies() == Change::All && owner.history().undo_steps == 0 &&
        owner.snapshot().shares_data(source));
  auto tx = plan.begin_edit();
  owner.commit(tx);
  auto converted = owner.snapshot();
  check(converted.data().dataset.pixel_type() == PixelType::UInt8 &&
        converted.display().properties.empty() && owner.history().undo_steps == 1);
  for (int i = 0; i < 8; ++i) {
    const auto pixels = converted.data().dataset.plane({i / 4, i / 2 % 2, i % 2});
    for (int j = 0; j < 6; ++j) check(pixels->pixels[j] == std::min(255, 100 * i + j));
  }
  owner.undo();
  check(metadata_equal(owner.snapshot().display().properties, display.properties));
  owner.redo();
  check(owner.snapshot().display().properties.empty());
  owner.undo();
  auto stale = prepare_native_workflow(owner.snapshot(), {op});
  display.t = 1;
  owner.set_display(display);
  refuses([&] { auto edit = stale.begin_edit(); owner.commit(edit); });
  check(owner.snapshot().data().dataset.pixel_type() == PixelType::UInt16);
}
void rgb_workflow_contract() {
  const DatasetShape shape{2,2,2,2,3,1};Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){Image image(3,2,1,PixelType::UInt16);for(auto &v:image.pixels)v=10+20*c;planes.push_back(std::move(image));}
  DatasetReadObserver reads;Document owner(Dataset::from_planes(shape,std::move(planes)).observe_reads(reads));
  auto view=owner.snapshot().display();ChannelDisplayState channels;channels.channels.resize(2);channels.channels[0].lut.name="Red";channels.channels[1].lut.name="Green";
  view.properties["channel_display"]=metadata_object(encode_channel_display(channels,shape,PixelType::UInt16,FloatingSamplePolicy::FiniteOnly));owner.set_display(view);
  RgbDatasetRequest request;request.mode=RgbDatasetMode::Composite;
  NativeWorkflowOperation rgb;rgb.operation="rgb_dataset";rgb.original=resolve_rgb_dataset_command(owner.snapshot(),request).record();
  rgb.target.scope=TargetScope::Ranges;rgb.target.roi=RoiPolicy::WholePlane;rgb.target.t=rgb.target.c=rgb.target.z=AxisRange{0,2,1};
  NativeWorkflowOperation first;first.operation="add";first.parameters={{"value",1.}};first.target=rgb.target;
  auto last=first;last.parameters={{"value",2.}};last.target.c=AxisRange{0,1,1};
  auto invalid=last;invalid.target.c=AxisRange{0,2,1};refuses([&]{prepare_native_workflow(owner.snapshot(),{first,rgb,invalid});});check(reads.counts().plane_reads==0);
  const auto program=native_workflow_program_from_json(make_native_workflow_program({first,rgb,last}).json());
  auto plan=prepare_native_workflow(owner.snapshot(),program.steps());check(plan.dependencies()==Change::All && owner.history().undo_steps==0);
  const auto &data=plan.preview().data().dataset;check(data.shape().c==1 && data.shape().s==3 && data.pixel_type()==PixelType::UInt8);
  for(int t=0;t<2;++t)for(int z=0;z<2;++z){const auto image=data.plane({t,0,z});for(int i=0;i<6;++i)check(image->pixels[3*i]==13 && image->pixels[3*i+1]==33 && image->pixels[3*i+2]==2);}
  auto edit=plan.begin_edit();owner.commit(edit);check(owner.history().undo_steps==1);owner.undo();check(owner.snapshot().data().dataset.shape().s==1);owner.redo();check(owner.snapshot().data().dataset.shape().c==1);
}
void color_workflow_contract() {
  Image image(3,2,3,PixelType::UInt8);for(std::size_t i=0;i<image.pixels.size();++i)image.pixels[i]=10+i;
  DatasetReadObserver reads;Document owner(Dataset::from_planes({2,2,2,2,3,3},Stack(8,image)).observe_reads(reads));
  auto view=owner.snapshot().display();view.t=view.c=1;view.crosshair={1,1,1};owner.set_display(view);
  NativeWorkflowOperation split;split.operation="color_stack";split.original=resolve_color_stack_command(owner.snapshot()).record();
  split.target.scope=TargetScope::Ranges;split.target.roi=RoiPolicy::WholePlane;split.target.t=split.target.c=split.target.z=AxisRange{0,2,1};
  NativeWorkflowOperation add;add.operation="add";add.parameters={{"value",1.}};add.target=split.target;add.target.c=AxisRange{0,6,1};
  auto merge=split;merge.target=add.target;
  merge.parameters={{"action",std::string("merge")},{"space",std::string("rgb")},{"layout",std::string("preserve_axes")},{"invert_rgb",false},{"allow_calibration_reset",false},{"allow_nonxy_annotation_drop",false},{"max_working_bytes",std::uint64_t(512ULL*1024*1024)}};
  auto program=native_workflow_program_from_json(make_native_workflow_program({split,add,merge}).json());check(reads.counts().plane_reads==0);
  const auto original_source=owner.snapshot();
  auto prepared=prepare_native_workflow(owner.snapshot(),program.steps());check(prepared.completed_steps()==3 && prepared.dependencies()==Change::All);
  auto expected=image.pixels;for(auto &v:expected)++v;check(prepared.preview().data().dataset.plane({1,1,1})->pixels==expected);
  check(prepared.preview().display().c==1);auto edit=prepared.begin_edit();owner.commit(edit);check(owner.undo_steps()==1);owner.undo();check(owner.snapshot().data().dataset.plane({1,1,1})->pixels==image.pixels);owner.redo();
  auto bad=merge;bad.parameters.erase("space");const auto count=reads.counts().plane_reads;
  refuses([&]{prepare_native_workflow(owner.snapshot(),{split,bad});});check(reads.counts().plane_reads==count);
  bad=merge;bad.target.c=AxisRange{0,2,1};refuses([&]{prepare_native_workflow(owner.snapshot(),{split,bad});});check(reads.counts().plane_reads==count);
  auto floating=split;floating.parameters=merge.parameters;floating.parameters["action"]=std::string("split");floating.parameters["space"]=std::string("hsb32");
  NativeWorkflowOperation threshold;threshold.operation="threshold";threshold.parameters={{"lower",.2},{"upper",.8}};threshold.target=merge.target;
  auto wrong_merge=merge;wrong_merge.parameters["space"]=std::string("hsb32");
  const auto no_reads=reads.counts().plane_reads;
  refuses([&]{prepare_native_workflow(original_source,{floating,threshold,wrong_merge});});check(reads.counts().plane_reads==no_reads);
  auto byte_merge=merge;byte_merge.parameters["space"]=std::string("hsb8");
  check(prepare_native_workflow(original_source,{floating,threshold,byte_merge}).completed_steps()==3);
}
void indexed_workflow_contract() {
  Image red(2,1,3,PixelType::UInt8);red.pixels={255,0,0,0,255,0};
  Image blue(2,1,3,PixelType::UInt8);blue.pixels={0,0,255,255,255,0};
  DatasetReadObserver reads;Document a(Dataset::from_planes({1,1,1,1,2,3},Stack{red}));
  Document b(Dataset::from_planes({1,1,1,1,2,3},Stack{blue}).observe_reads(reads));
  IndexedDatasetRequest request;request.max_colors=2;
  auto capture=resolve_indexed_dataset_command(a.snapshot(),request);
  auto indexed=prepare_indexed_dataset(capture).preview();
  auto expand=resolve_indexed_rgb_command(indexed);
  validate_indexed_rgb_command_record(expand.record());
  NativeWorkflowOperation quantize;quantize.operation="indexed_dataset";quantize.original=capture.record();
  quantize.target.scope=TargetScope::Ranges;quantize.target.roi=RoiPolicy::WholePlane;
  quantize.target.t=quantize.target.c=quantize.target.z=AxisRange{0,1,1};
  auto rgb=quantize;rgb.operation="indexed_rgb";rgb.original=expand.record();
  rgb.parameters={{"palette_source",std::string("stored")},{"max_working_bytes",std::uint64_t(512ULL*1024*1024)}};
  auto program=native_workflow_program_from_json(make_native_workflow_program({quantize,rgb}).json());
  check(reads.counts().plane_reads==0);
  auto bad=rgb;bad.target.c=AxisRange{0,2,1};refuses([&]{prepare_native_workflow(b.snapshot(),{quantize,bad});});check(reads.counts().plane_reads==0);
  auto result=prepare_native_workflow(b.snapshot(),program.steps());check(result.dependencies()==Change::All && result.completed_steps()==2);
  check(result.preview().data().dataset.plane({0,0,0})->pixels==blue.pixels);
  check(metadata_equal(*std::get<NativeWorkflowOperation>(program.steps()[1]).original,expand.record()));
  auto edit=result.begin_edit();b.commit(edit);check(b.undo_steps()==1);b.undo();b.redo();
  check(b.snapshot().data().dataset.plane({0,0,0})->pixels==blue.pixels);
}
} // namespace
int main() {
  try {
    DatasetShape shape{2, 3, 2, 4, 5, 1};
    Stack planes;
    for (std::size_t i = 0; i < shape.plane_count(); ++i) {
      Image im(5, 4, 1, PixelType::UInt16);
      std::fill(im.pixels.begin(), im.pixels.end(), double(i));
      planes.push_back(std::move(im));
    }
    DatasetReadObserver reads;
    Document doc(
        Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
    auto before = doc.snapshot();
    ImageJRoi row;
    row.roi.name = "Original";
    row.roi.selection = Selection("rectangle", {{1, 1}, {3, 3}});
    ImageJRoiPlaneMapping mapping;
    mapping.extent = shape;
    mapping.coordinate = 0;
    ImageJRoiImportItem item{
        "original.roi", encode_imagej_roi(row), mapping, {}};
    NativeWorkflowImport import{{item}};
    NativeWorkflowOperation add;
    add.operation = "add";
    add.parameters = {{"value", std::int64_t(5)}};
    add.target.scope = TargetScope::Positions;
    add.target.positions = {{1, 2, 1}};
    add.target.roi = RoiPolicy::WholePlane;
    ImageJAnnotationExportItem choice;
    choice.name = "native.roi";
    choice.source_index = 0;
    choice.annotation_index = 0;
    row.roi.z = 0;
    choice.standard_projection = row;
    NativeWorkflowExport out;
    out.name = "first.zip";
    out.items = {choice};
    auto second = out;
    second.name = "second.zip";
    refuses([&] {
      prepare_native_workflow(before, {import, out, add, import, out});
    });
    auto p =
        prepare_native_workflow(before, {import, out, add, import, second});
    check(p.completed_steps() == 5 && p.outputs().size() == 2 &&
          doc.snapshot().shares_data(before));
    check(p.preview().data().dataset.shape().dimensions() ==
          shape.dimensions());
    check(p.outputs()[0].value.source().data().dataset.plane({1, 2, 1})->at(
              0, 0) == 11);
    check(p.outputs()[1].value.source().data().dataset.plane({1, 2, 1})->at(
              0, 0) == 16);
    check(planar_annotations(p.preview().data()).size() == 2);
    auto edit = p.begin_edit();
    doc.commit(edit);
    check(doc.undo_steps() == 1);
    doc.undo();
    check(doc.snapshot().shares_data(before));
    doc.redo();
    check(planar_annotations(doc.snapshot().data()).size() == 2);
    auto bad = import;
    std::get<ImageJRoiImportItem>(bad.items[0]).data = {1, 2, 3};
    auto counts = reads.counts();
    refuses([&] { prepare_native_workflow(before, {add, bad}); });
    check(reads.counts().plane_reads == counts.plane_reads);
    counts = reads.counts();
    refuses([&] {
      prepare_native_workflow(before, {add}, NativeWorkflowLimits{64, 1});
    });
    check(reads.counts().plane_reads == counts.plane_reads);
    auto canceled = std::make_shared<CancellationToken>();
    ExecutionControl control;
    control.cancellation = canceled;
    control.stage_progress = [&](std::string_view, std::size_t n, std::size_t) {
      if (n == 1)
        canceled->cancel();
    };
    refuses(
        [&] { prepare_native_workflow(before, {import, add}, {}, control); });
    auto stale = p.begin_edit();
    refuses([&] { doc.commit(stale); });
    auto empty = prepare_native_workflow(doc.snapshot(), {});
    auto no_op = empty.begin_edit();
    const auto undo = doc.undo_steps();
    doc.commit(no_op);
    check(doc.undo_steps() == undo);
    auto g = resolve_gaussian_command(before, {},
                                      GaussianBlurCommandOptions{1, false});
    NativeWorkflowOperation replay;
    replay.operation = "gaussian_blur_imagej";
    replay.original = g.record();
    replay.target.roi = RoiPolicy::WholePlane;
    check(prepare_native_workflow(before, {replay}).completed_steps() == 1);
    contrast_workflow_contract();
    histogram_workflow_contract();
    conversion_contract();
    rgb_workflow_contract();
    color_workflow_contract();
    indexed_workflow_contract();
    std::cout
        << "PASS native scientific workflow planning and atomic proposal\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
