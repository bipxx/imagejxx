#include "rank_display_internal.hpp"
#include "operation_internal.hpp"
#include "axis_command_record.hpp"
#include <filesystem>
#include <imagejxx/rank_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/channel_display.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace imagejxx;
namespace {
using Json = nlohmann::json;
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class Error, class F> void rejects(F function, const char *message) {
  bool refused = false;
  try { function(); } catch (const Error &) { refused = true; }
  check(refused, message);
}
Image decode(const Json &row, const std::string &hex) {
  const bool floating = row.at("dtype") == "float32";
  const std::size_t bytes = floating ? 4 : 2;
  Image result(25,20,1,floating ? PixelType::Float32 : PixelType::UInt16);
  check(hex.size() == 500 * 2 * bytes, "Observed byte count differs");
  for (std::size_t i = 0; i < 500; ++i) {
    std::uint32_t word = 0;
    for (std::size_t b = 0; b < bytes; ++b)
      word |= std::stoul(hex.substr((i * bytes + b) * 2, 2), nullptr, 16) << (8 * b);
    if (floating) { float value;std::memcpy(&value,&word,4);result.pixels[i] = double(value); }
    else result.pixels[i] = double(word);
  }
  return result;
}
Dataset plane(Image image) {
  const DatasetShape shape{1,1,1,image.height,image.width,1};
  Stack stack;stack.push_back(std::move(image));
  return Dataset::from_planes(shape,std::move(stack));
}
void references(const char *path) {
  std::ifstream file(path);check(bool(file),"Missing plugin-runner fixture");
  const auto fixture = Json::parse(file);
  check(fixture.at("imagej_commit") == "62db90a269cccc1ac79f658e4602043214ed055b","Wrong reference commit");
  check(fixture.at("cases").size() == 288,"Runner matrix changed");
  std::size_t retained = 0;
  for (const auto &row : fixture.at("cases")) {
    check(row.at("current_slice") == 2,"Plugin runner changed active slice");
    check(row.at("roi_retained").get<bool>() == (row.at("selection") != 0),"Runner ROI observation changed");
    Stack stack;
    for (const auto &hex : row.at("output_hex")) stack.push_back(decode(row,hex.get<std::string>()));
    const auto dataset = Dataset::from_planes({1,1,3,20,25,1},std::move(stack));
    DatasetReadObserver observer;const auto observed = dataset.observe_reads(observer);
    const auto estimate = detail::estimate_rank_variance_final_range(observed,{0,0,1});
    check(estimate.pixel_count == 500 && observer.counts().plane_reads == 0,"Estimate acquired pixels");
    const auto range = detail::rank_variance_final_range_imagej(observed,{0,0,1});
    check(observer.counts().plane_reads == 1,"Final histogram must capture only one active plane");
    if (!range) ++retained;
    const auto result = range.value_or(row.at("initial_range").get<std::array<double,2>>());
    const auto expected = row.at("final_range").get<std::array<double,2>>();
    check(std::memcmp(result.data(),expected.data(),sizeof(result)) == 0,"Final display differs from ImageJ runner bits");
    if (row.at("mode") == 0) {
      check(row.at("input_hex").at(0) == row.at("output_hex").at(0) && row.at("input_hex").at(2) == row.at("output_hex").at(2),"Current-only runner modified untargeted planes");
    }
  }
  check(retained > 0,"Constant-image range retention was not covered");
  std::cout << "288 runner references, " << retained << " retained ranges\n";
}
struct Descriptor : PlaneSource {
  mutable int reads = 0;
  DatasetShape shape() const override { return {1,1,1,4096,4096,1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;throw std::runtime_error("Budget refusal must precede acquisition");
  }
};
void controls() {
  Image image(20,20,1,PixelType::UInt16);std::fill(image.pixels.begin(),image.pixels.end(),65535);
  const auto original = plane(image);DatasetReadObserver observer;
  const auto observed = original.observe_reads(observer);
  auto token = std::make_shared<CancellationToken>();token->cancel();
  int callbacks = 0;ExecutionControl control;control.cancellation = token;
  control.checkpoint = [&]{ ++callbacks; };
  rejects<OperationCancelled>([&]{detail::rank_variance_final_range_imagej(observed,{},control);},"Pre-cancel did not refuse");
  check(callbacks == 0 && observer.counts().plane_reads == 0,"Pre-cancel read/called back");
  const auto huge = std::make_shared<Descriptor>();
  rejects<std::invalid_argument>([&]{detail::estimate_rank_variance_final_range(Dataset(huge),{});},"Working bound not admitted before pixels");
  control = {};control.checkpoint = [&]{++callbacks;};
  rejects<std::invalid_argument>([&]{detail::rank_variance_final_range_imagej(Dataset(huge),{},control);},"Large source entered histogram");
  Document owner{Dataset(huge)};
  rejects<std::invalid_argument>([&]{resolve_rank_command(owner.snapshot(),{},
      {RankFilterMethod::Variance,1});},"Variance command missed pre-read histogram admission");
  check(huge->reads == 0 && callbacks == 0,"Budget refusal read/called back");
  for (bool cancel : {false,true}) {
    token = std::make_shared<CancellationToken>();control = {};control.cancellation = token;
    bool reached = false;
    control.stage_progress = [&](std::string_view stage,std::size_t done,std::size_t) {
      if (stage == "ImageJ variance display tails" && done == 1024) {
        reached = true;
        if (cancel) token->cancel(); else throw std::logic_error("callback failure");
      }
    };
    if (cancel) rejects<OperationCancelled>([&]{detail::rank_variance_final_range_imagej(observed,{},control);},"Tail cancellation not propagated");
    else rejects<std::logic_error>([&]{detail::rank_variance_final_range_imagej(observed,{},control);},"Tail callback failure not propagated");
    check(reached,"Tail scan did not poll within 1024 bins");
  }
  Dataset mutable_source = observed;
  control = {};bool changed = false;
  control.checkpoint = [&]{
    if (changed) return;
    changed = true;
    mutable_source = plane(Image(2,2,1,PixelType::Float64));
    control.cancellation = std::make_shared<CancellationToken>();control.cancellation->cancel();
    control.checkpoint = []{throw std::logic_error("uncaptured control");};
  };
  check(!detail::rank_variance_final_range_imagej(mutable_source,{},control) && changed,
        "Source/control values were not captured before callbacks");
  check(original.plane({})->pixels == image.pixels,"Detached failure altered source storage");
  for (const auto type : {PixelType::UInt8,PixelType::Float64}) {
    rejects<std::invalid_argument>([&]{detail::rank_variance_final_range_imagej(plane(Image(2,2,1,type)),{});},"Unsupported type entered nonbyte display profile");
  }
  Image invalid(2,2,1,PixelType::Float32);invalid.pixels.back() = double(std::numeric_limits<float>::infinity());
  rejects<std::invalid_argument>([&]{detail::rank_variance_final_range_imagej(plane(invalid),{});},"Nonfinite source accepted");
}
void initialRanges(const char *path) {
  std::ifstream file(std::filesystem::path(path).parent_path()/"variance-initial-range-imagej154r.json");
  check(bool(file),"Missing initial-range runner corpus");const auto fixture=Json::parse(file);
  check(fixture.at("cases").size()==16,"Initial-range matrix changed");
  for(const auto &row:fixture.at("cases")) {
    const bool floating=row.at("dtype")=="float32";
    Image image(8,4,1,floating?PixelType::Float32:PixelType::UInt16);
    for(std::size_t i=0;i<32;++i)image.pixels[i]=row.at("signal")==0?7.0:floating?1000000.0:(i%2)*65535.0;
    Document owner(plane(image));auto display=owner.snapshot().display();
    if(row.at("choice")!=3)display.properties["display_range"]=metadata_array({row.at("requested").at(0).get<double>(),row.at("requested").at(1).get<double>()});
    owner.set_display(display);const auto before=owner.snapshot();
    const auto command=resolve_rank_command(before,{}, {RankFilterMethod::Variance,0});
    const auto prepared=prepare_operation("rank_filter_imagej",command.target(),command.parameters());
    const auto &range=detail::axis_command_record::array_value(prepared.preview().display().properties.at("display_range"));
    check(detail::axis_command_record::number_value(range[0])==row.at("final").at(0).get<double>() &&
          detail::axis_command_record::number_value(range[1])==row.at("final").at(1).get<double>(),"Effective initial range differs from runner");
    auto edit=prepared.begin_edit();owner.commit(edit);owner.undo();
    check(metadata_equal(owner.snapshot().display().properties,before.display().properties),"Initial-range undo did not restore requested properties");
  }
}
void thresholdReferences(const char *path) {
  std::ifstream file(std::filesystem::path(path).parent_path()/"variance-threshold-imagej154r.json");
  check(bool(file),"Missing thresholded variance runner corpus");const auto fixture=Json::parse(file);
  check(fixture.at("cases").size()==72,"Threshold runner matrix changed");
  for(const auto &row:fixture.at("cases")) {
    Stack stack;
    for(const auto &hex:row.at("input_hex"))stack.push_back(decode(row,hex.get<std::string>()));
    DatasetReadObserver reads;
    DocumentData data{Dataset::from_stack(std::move(stack)).observe_reads(reads)};
    if(row.at("selection")==1)data.selection=Selection("rectangle",{{2,3},{21,16}});
    else if(row.at("selection")==2)data.selection=Selection("polygon",{{2,3},{20,2},{18,17},{5,15}});
    Document owner(std::move(data));auto display=owner.snapshot().display();display.crosshair={12,10,1};
    display.properties["display_range"]=metadata_array({7.,2047.});
    set_manual_threshold_state(display,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});
    owner.set_display(display);const auto before=owner.snapshot();
    TargetRequest request;if(row.at("mode")==1)request.scope=TargetScope::AllZ;
    const auto command=resolve_rank_command(before,request,{RankFilterMethod::Variance,row.at("radius").get<double>()});
    check(reads.counts().plane_reads==0,"Threshold admission read samples");
    const auto prepared=prepare_operation("rank_filter_imagej",command.target(),command.parameters());
    const auto threshold=manual_threshold_state(prepared.preview().display(),prepared.preview().data().dataset.shape());
    check(bool(threshold)==!row.at("final_threshold").is_null(),"Threshold reset differs from actual runner");
    if(threshold)check(threshold->range.lower==4. && threshold->range.upper==8.,"Retained threshold bounds changed");
    const auto &range=detail::axis_command_record::array_value(prepared.preview().display().properties.at("display_range"));
    check(detail::axis_command_record::number_value(range[0])==row.at("final_range").at(0).get<double>() &&
          detail::axis_command_record::number_value(range[1])==row.at("final_range").at(1).get<double>(),"Thresholded final bounds differ");
    for(int z=0;z<3;++z)check(prepared.preview().data().dataset.plane({0,0,z})->pixels==decode(row,row.at("output_hex").at(z).get<std::string>()).pixels,"Threshold changed variance pixels");
    auto edit=prepared.begin_edit();owner.commit(edit);owner.undo();
    check(metadata_equal(owner.snapshot().display().properties,before.display().properties),"Threshold undo lost source display");
    owner.redo();check(metadata_equal(owner.snapshot().display().properties,prepared.preview().display().properties),"Threshold redo lost paired display");
  }
}
void channelReferences(const char *path) {
  std::ifstream file(std::filesystem::path(path).parent_path()/"variance-channel-imagej154r.json");
  check(bool(file),"Missing channel variance runner corpus");const auto fixture=Json::parse(file);
  check(fixture.at("cases").size()==48,"Channel runner matrix changed");
  for(const auto &row:fixture.at("cases")) {
    const bool floating=row.at("dtype")=="float32";
    auto decodePlane=[&](const std::string &hex) {
      Image image(5,5,1,floating?PixelType::Float32:PixelType::UInt16);const int bytes=floating?4:2;
      check(hex.size()==std::size_t(25*2*bytes),"Channel plane byte count changed");
      for(std::size_t i=0;i<25;++i){std::uint32_t word=0;
        for(int b=0;b<bytes;++b)word|=std::stoul(hex.substr((i*bytes+b)*2,2),nullptr,16)<<(8*b);
        if(floating){float value;std::memcpy(&value,&word,4);image.pixels[i]=double(value);}else image.pixels[i]=word;
      }return image;
    };
    Stack stack;for(int i:{0,2,1,3})stack.push_back(decodePlane(row.at("input_hex").at(i).get<std::string>()));
    DatasetReadObserver reads;
    Document owner(Dataset::from_planes({1,2,2,5,5,1},std::move(stack)).observe_reads(reads));
    auto display=owner.snapshot().display();display.c=1;display.crosshair={2,2,1};
    display.properties["display_range"]=metadata_array({-3.,700.});
    ChannelDisplayState channels;
    channels.mode=row.at("mode")==1?ChannelDisplayMode::Composite:row.at("mode")==2?ChannelDisplayMode::Color:ChannelDisplayMode::Grayscale;
    for(int c=0;c<2;++c)channels.channels.push_back({true,row.at("initial_lut_ranges").at(c).get<std::array<double,2>>(),{c==0?"Red":"Green",{}}});
    display=with_channel_display(display,owner.snapshot().data().dataset,channels);
    set_manual_threshold_state(display,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,1,1}});
    owner.set_display(display);const auto before=owner.snapshot();
    TargetRequest request;if(row.at("all")==1){request.scope=TargetScope::Ranges;request.t=AxisRange{0,1,1};request.c=AxisRange{0,2,1};request.z=AxisRange{0,2,1};}
    const auto command=resolve_rank_command(before,request,{RankFilterMethod::Variance,0});
    check(reads.counts().plane_reads==0,"Channel variance admission read samples");
    const auto prepared=prepare_operation("rank_filter_imagej",command.target(),command.parameters());
    const auto after=prepared.preview();
    for(int z=0;z<2;++z)for(int c=0;c<2;++c)
      check(after.data().dataset.plane({0,c,z})->pixels==decodePlane(row.at("output_hex").at(z*2+c).get<std::string>()).pixels,"Channel variance changed wrong plane/pixels");
    auto expected=display;channels.channels[1].range=row.at("final_processor_range").get<std::array<double,2>>();
    expected=with_channel_display(expected,after.data().dataset,channels);
    if(row.at("final_threshold").is_null())reset_manual_threshold_state(expected);
    check(metadata_equal(after.display().properties,expected.properties),"Canonical active range/threshold publication differs");
    const auto rgb=compose_channel_display(after.data().dataset,channels,Plane::XY,1,0,1);
    const auto raw=row.at("rgb_hex").get<std::string>();check(raw.size()==150,"Channel RGB reference extent changed");
    for(std::size_t i=0;i<75;++i)check(rgb.pixels[i]==std::stoul(raw.substr(i*2,2),nullptr,16),"Channel rendered RGB differs from runner");
    auto edit=prepared.begin_edit();owner.commit(edit);owner.undo();
    check(metadata_equal(owner.snapshot().display().properties,before.display().properties),"Channel variance undo lost source presentation");
    owner.redo();check(metadata_equal(owner.snapshot().display().properties,expected.properties),"Channel variance redo lost final presentation");
  }
}
void operationProgress() {
  Stack stack;
  for(int i=0;i<3;++i){Image image(4,4,1,PixelType::UInt16);std::fill(image.pixels.begin(),image.pixels.end(),7);stack.push_back(std::move(image));}
  Document owner(Dataset::from_stack(std::move(stack)));
  TargetRequest request;request.scope=TargetScope::AllZ;
  const auto command=resolve_rank_command(owner.snapshot(),request,{RankFilterMethod::Variance,0});
  std::vector<std::pair<std::size_t,std::size_t>> progress;bool histogram_stage=false;
  ExecutionControl control;
  control.progress=[&](std::size_t done,std::size_t total){progress.emplace_back(done,total);};
  control.stage_progress=[&](std::string_view stage,std::size_t,std::size_t){
    if(stage=="Dataset histogram validation/extrema")histogram_stage=true;
  };
  (void)prepare_operation("rank_filter_imagej",command.target(),command.parameters(),{},control);
  check(progress==std::vector<std::pair<std::size_t,std::size_t>>{{0,3},{1,3},{2,3},{3,3}},
        "Nested final histogram overwrote plane progress");
  check(histogram_stage,"Final histogram stages were hidden with plane progress");
}
void tails() {
  const OperationParameters parameters{{"method",std::string("variance")},{"radius",1.0}};
  const auto byte_extra=operation_extra_workspace("rank_filter_imagej",{20,20,1,PixelType::UInt8,{}},parameters);
  const auto short_extra=operation_extra_workspace("rank_filter_imagej",{20,20,1,PixelType::UInt16,{}},parameters);
  check(short_extra > byte_extra && short_extra-byte_extra > 1024*1024,
        "Nonbyte rank jobs omitted final histogram buffers from admission");
  for (int n : {399,400}) {
    Image image(n,1,1,PixelType::UInt16);std::fill(image.pixels.begin(),image.pixels.end(),5);
    image.pixels.front() = 0;image.pixels.back() = 9;
    const auto range = detail::rank_variance_final_range_imagej(plane(image),{});
    if (n == 399) check(range == std::optional<std::array<double,2>>(std::array<double,2>{0,9}),"Zero-threshold tails changed");
    else check(!range,"One-sample strict tail cutoff must retain old range when both bins coincide");
    image.pixels[1] = 0;image.pixels[n-2] = 9;
    check(detail::rank_variance_final_range_imagej(plane(image),{}) == std::optional<std::array<double,2>>(std::array<double,2>{0,9}),"Strict cumulative > tail threshold changed");
  }
}
} // namespace
int main(int argc,char **argv) {
  try {
    check(argc == 2,"Pass the variance display fixture");references(argv[1]);controls();tails();initialRanges(argv[1]);thresholdReferences(argv[1]);channelReferences(argv[1]);operationProgress();
    std::cout << checks << " variance display checks passed\n";return 0;
  } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
