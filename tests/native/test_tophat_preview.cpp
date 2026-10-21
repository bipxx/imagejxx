#include "rank_display_internal.hpp"
#include <imagejxx/rank_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/application.hpp>
#include "axis_command_record.hpp"
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
void previews(const char *path) {
 const auto held=corpus(path,216);
 for(const auto &row:held.at("cases")) {
  const auto image=decode(row,row.at("input_hex").at(0),5,5);
  Document owner(Dataset::from_stack(Stack{image}));
  if(row.at("selection")==1){auto edit=owner.snapshot().begin_edit();edit.select(Selection("polygon",{{0,0},{4,1},{2,4}}));owner.commit(edit);}
  auto view=owner.snapshot().display();const auto range=row.at("initial_range").get<std::array<double,2>>();
  view.properties["display_range"]=metadata_array({range[0],range[1]});
  set_manual_threshold_state(view,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,0,0}});
  owner.set_display(view);const auto before=owner.snapshot();
  const auto undo_before=owner.history().undo_steps, redo_before=owner.history().redo_steps;
  const auto command=resolve_top_hat_command(before,{}, {1,row.at("light_background"),row.at("dont_subtract")});
  ExecutionOptions options;options.phase=ExecutionPhase::Preview;
  auto proposal=prepare_operation(command.operation(),command.target(),command.parameters(),options);
  const auto display=proposal.preview().display();
  const auto actual=detail::axis_command_record::array_value(display.properties.at("display_range"));
  check(detail::axis_command_record::number_value(actual[0])==row.at("final_range").at(0).get<double>() &&
        detail::axis_command_record::number_value(actual[1])==row.at("final_range").at(1).get<double>(),"Prepared preview range differs from direct processor");
  check(!manual_threshold_state(display,before.data().dataset.shape())==row.at("final_threshold").is_null(),"Prepared preview threshold differs");
  const auto region=rank_command_region(command.target(),0);
  auto expected=decode(row,row.at("processor_hex"),5,5);
  if(region.mask)for(std::size_t i=0;i<expected.pixels.size();++i)if(!region.mask->pixels[i])expected.pixels[i]=image.pixels[i];
  check(proposal.preview().data().dataset.plane({})->pixels==expected.pixels,"Preview masked pixels differ");
  refuses<std::invalid_argument>([&]{proposal.begin_edit();});
  refuses<std::invalid_argument>([&]{proposal.stage_new_document();});
  Application app;auto owned=std::make_shared<Document>(before);app.adopt(owned);
  refuses<std::invalid_argument>([&]{app.publish(proposal);});
  check(owner.history().undo_steps==undo_before && owner.history().redo_steps==redo_before,"Preview changed source history");
  check(owner.snapshot().data().dataset.plane({})->pixels==image.pixels && metadata_equal(owner.snapshot().display().properties,before.display().properties),"Preview mutated source pixels or display");
 }
}
void full_bbox_mask_phase(const char *path){
 const auto reference=corpus(path,2);
 for(const auto &row:reference.at("cases")) {
  const Json tag={{"dtype","float32"}};
  const auto image=decode(tag,row.at("input_hex"),5,5);
  Document owner(Dataset::from_stack(Stack{image}));auto edit=owner.snapshot().begin_edit();
  edit.select(Selection("polygon",{{0,0},{5,0},{0,5}}));owner.commit(edit);
  auto view=owner.snapshot().display();view.properties["display_range"]=metadata_array({7.,127.});
  set_manual_threshold_state(view,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,0,0}});owner.set_display(view);
  const auto command=resolve_top_hat_command(owner.snapshot(),{}, {1,false,false});
  ExecutionOptions options;options.phase=row.at("preview").get<bool>()?ExecutionPhase::Preview:ExecutionPhase::Apply;
  const auto result=prepare_operation(command.operation(),command.target(),command.parameters(),options);
  const auto range=detail::axis_command_record::array_value(result.preview().display().properties.at("display_range"));
  check(detail::axis_command_record::number_value(range[0])==row.at("final_range").at(0).get<double>() && detail::axis_command_record::number_value(range[1])==row.at("final_range").at(1).get<double>(),"Full-bounds mask phase range differs from ImageJ");
  check(result.preview().data().dataset.plane({})->pixels==decode(tag,row.at("output_hex"),5,5).pixels,"Full-bounds mask phase pixels differ from ImageJ");
  check(!manual_threshold_state(result.preview().display(),owner.snapshot().data().dataset.shape()),"Full-bounds mask retained threshold");
  check(owner.snapshot().data().dataset.plane({})->pixels==image.pixels,"Mask phase mutated source");
 }
}
void phase_guards(){
 Stack planes{Image(5,5,1,PixelType::UInt8),Image(5,5,1,PixelType::UInt8)};
 auto owner=std::make_shared<Document>(Dataset::from_stack(planes));Application app;app.adopt(owner);
 TargetRequest request;request.scope=TargetScope::AllZ;const auto command=resolve_top_hat_command(owner->snapshot(),request);
 ExecutionOptions options;options.phase=ExecutionPhase::Preview;
 refuses<std::invalid_argument>([&]{prepare_operation(command.operation(),command.target(),command.parameters(),options);});
 auto one=resolve_top_hat_command(owner->snapshot());
 auto invalid=options;invalid.phase=static_cast<ExecutionPhase>(99);
 refuses<std::invalid_argument>([&]{prepare_operation(one.operation(),one.target(),one.parameters(),invalid);});
 auto job=app.submit(one.operation(),one.target(),one.parameters(),options);job.wait();
 check(job.prepared().options().phase==ExecutionPhase::Preview,"Scheduler lost preview phase");
 auto proposal=job.prepared();refuses<std::invalid_argument>([&]{app.publish(proposal);});
 check(owner->history().undo_steps==0,"Transient scheduler proposal changed source");
}

}
int main(int argc,char **argv) {
 try{check(argc==3,"Pass preview and full-bounds-mask fixtures");previews(argv[1]);phase_guards();full_bbox_mask_phase(argv[2]);std::cout<<checks<<" Top Hat preview preparation checks passed\n";return 0;}
 catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
