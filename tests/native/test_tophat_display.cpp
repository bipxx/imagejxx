#include "rank_display_internal.hpp"
#include <imagejxx/rank.hpp>
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
using Json = nlohmann::json;
using Lane = detail::RankProcessorLane;
namespace {
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class E, class F> void refuses(F f) {
  bool threw = false;
  try { f(); } catch (const E &) { threw = true; }
  check(threw, "Invalid final display request admitted");
}
Image decode(const Json &row, const std::string &hex, int width, int height) {
  const auto type = row.at("dtype") == "uint8" ? PixelType::UInt8 :
      row.at("dtype") == "uint16" ? PixelType::UInt16 : PixelType::Float32;
  const int bytes = type == PixelType::UInt8 ? 1 : type == PixelType::UInt16 ? 2 : 4;
  Image image(width,height,1,type);
  check(hex.size() == image.pixels.size()*bytes*2, "Reference shape differs");
  for (std::size_t i=0; i<image.pixels.size(); ++i) {
    std::uint32_t word=0;
    for(int j=0;j<bytes;++j)
      word |= std::stoul(hex.substr((i*bytes+j)*2,2),nullptr,16) << (8*j);
    if(type==PixelType::Float32) {float v;std::memcpy(&v,&word,4);image.pixels[i]=v;}
    else image.pixels[i]=word;
  }
  return image;
}
Json corpus(const char *path, std::size_t count) {
  std::ifstream f(path);check(bool(f),"Missing Top Hat reference");
  auto result=Json::parse(f);
  check(result.at("cases").size()==count,"Top Hat reference count changed");
  check(result.at("imagej_commit")=="62db90a269cccc1ac79f658e4602043214ed055b","Reference commit changed");
  return result;
}
void compare(const Json &row, const Dataset &dataset, PlanePosition active,
             Lane lane, const char *initial, const char *final,
             std::optional<std::array<double,2>> setter_range = std::nullopt) {
  const bool dont=row.at("dont_subtract");
  DatasetReadObserver observer;const auto observed=dataset.observe_reads(observer);
  detail::validate_top_hat_final_display_source(observed,active,dont,lane);
  check(observer.counts().plane_reads==0,"Display admission read pixels");
  const auto effects=detail::top_hat_final_display_imagej(observed,active,dont,lane,setter_range);
  check(observer.counts().plane_reads==std::uint64_t(!dont && row.at("dtype")!="uint8"),
        "Finalization read an unexpected number of planes");
  const auto actual=effects.range.value_or(row.at(initial).get<std::array<double,2>>());
  const auto expected=row.at(final).get<std::array<double,2>>();
  if(std::memcmp(actual.data(),expected.data(),sizeof(actual))!=0)std::cerr<<row.dump()<<" actual="<<actual[0]<<","<<actual[1]<<"\n";
  check(std::memcmp(actual.data(),expected.data(),sizeof(actual))==0,"Top Hat final range differs from upstream bits");
  check(effects.reset_threshold==row.at("final_threshold").is_null(),"Top Hat threshold reset differs from upstream");
  if(dont)check(!effects.range && !effects.reset_threshold,"Background-only changed presentation");
}
void runner(const char *path) {
  const auto held=corpus(path,576);
  for(const auto &row:held.at("cases")) {
    Stack stack;
    for(const auto &raw:row.at("output_hex"))stack.push_back(decode(row,raw.get<std::string>(),25,20));
    compare(row,Dataset::from_stack(std::move(stack)),{0,0,1},
        row.at("mode")==0?Lane::LiveProcessor:Lane::DetachedPlanes,"initial_range","final_range");
  }
}
void channels(const char *path) {
  const auto held=corpus(path,144);
  for(const auto &row:held.at("cases")) {
    Stack stack;
    for(const auto &raw:row.at("output_hex"))stack.push_back(decode(row,raw.get<std::string>(),5,5));
    // ImageJ stack order is C then Z; no conversion or channel rendering is
    // inferred from the observed transient processor range.
    compare(row,Dataset::from_planes({1,2,2,5,5,1},std::move(stack)),{0,1,1},
        row.at("all")==0?Lane::LiveProcessor:Lane::DetachedPlanes,
        "initial_processor_range","final_processor_range");
  }
}
void previews(const char *path) {
  const auto held=corpus(path,216);
  for(const auto &row:held.at("cases")) {
    const auto source=decode(row,row.at("input_hex").at(0),5,5);
    const auto extent=row.at("selection")==1?std::optional<RankFilterExtent>({0,0,4,4}):std::nullopt;
    const auto pixels=detail::top_hat_pixels_imagej(source,1,row.at("light_background"),
        row.at("dont_subtract"),false,extent,row.at("initial_range").get<std::array<double,2>>());
    check(pixels.image.pixels==decode(row,row.at("processor_hex"),5,5).pixels,
          "Direct preview processor pixels differ from native compound pass");
    compare(row,Dataset::from_stack(Stack{pixels.image}),{},Lane::LiveProcessor,
        "initial_range","final_range",pixels.subtraction_range);
  }
  // A polygon preview is stretched before mask restoration. Stretching a
  // completed masked image instead can produce a different range.
  Image original(5,5,1,PixelType::UInt16);original.pixels.assign(25,7);
  auto raw=top_hat_imagej(original,1);
  const auto before=detail::top_hat_final_display_imagej(Dataset::from_stack(Stack{raw}),{},false,Lane::LiveProcessor);
  raw.pixels[0]=7;
  const auto after=detail::top_hat_final_display_imagej(Dataset::from_stack(Stack{raw}),{},false,Lane::LiveProcessor);
  check(!before.range && after.range==std::optional<std::array<double,2>>(std::array<double,2>{0,7}),
        "Preview/apply histogram distinction was lost");
}
struct Descriptor : PlaneSource {
  mutable int reads=0;
  DatasetShape shape() const override{return {1,1,1,4096,4096,1};}
  PixelType pixel_type() const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition) const override{
    ++reads;throw std::runtime_error("Refused request acquired pixels");
  }
};
void controls() {
  auto held=std::make_shared<Descriptor>();const Dataset huge(held);
  refuses<std::invalid_argument>([&]{detail::top_hat_final_display_imagej(huge,{},false,Lane::LiveProcessor);});
  const auto skipped=detail::top_hat_final_display_imagej(huge,{},true,Lane::DetachedPlanes);
  check(!skipped.range && !skipped.reset_threshold && held->reads==0,"Background-only did histogram work");
  Image image(5,5,1,PixelType::UInt16);DatasetReadObserver reads;
  const auto dataset=Dataset::from_stack(Stack{image}).observe_reads(reads);
  refuses<std::invalid_argument>([&]{detail::top_hat_final_display_imagej(dataset,{},false,static_cast<Lane>(99));});
  refuses<std::invalid_argument>([&]{detail::top_hat_final_display_imagej(dataset,{0,0,1},true,Lane::LiveProcessor);});
  ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();
  refuses<OperationCancelled>([&]{detail::top_hat_final_display_imagej(dataset,{},false,Lane::LiveProcessor,{},control);});
  refuses<OperationCancelled>([&]{detail::top_hat_final_display_imagej(dataset,{},true,Lane::LiveProcessor,{},control);});
  check(reads.counts().plane_reads==0,"Invalid/pre-cancelled display request read samples");
  refuses<std::invalid_argument>([]{detail::top_hat_final_display_imagej(Dataset::from_stack(Stack{Image(2,2,3,PixelType::UInt8)}),{},true,Lane::LiveProcessor);});
  refuses<std::invalid_argument>([]{detail::top_hat_final_display_imagej(Dataset::from_stack(Stack{Image(2,2,1,PixelType::Float64)}),{},true,Lane::LiveProcessor);});
}
}
int main(int argc,char **argv) {
  try {
    check(argc==4,"Pass runner, channel and direct-preview fixtures");
    runner(argv[1]);channels(argv[2]);previews(argv[3]);controls();
    std::cout<<checks<<" Top Hat final display checks passed\n";return 0;
  } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
