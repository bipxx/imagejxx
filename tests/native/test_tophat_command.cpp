#include <imagejxx/rank_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/channel_display.hpp>
#include "kernel_control.hpp"
#include "operation_internal.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
std::size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
template<class F> void refuses(F f){bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;}check(threw,"Invalid Top Hat input admitted");}
Image decode(const Json &row,const std::string &raw){
  const auto type=row.at("dtype")=="uint8"?PixelType::UInt8:row.at("dtype")=="uint16"?PixelType::UInt16:PixelType::Float32;
  const int bytes=type==PixelType::UInt8?1:type==PixelType::UInt16?2:4;
  check(raw.size()==std::size_t(500*bytes*2),"Top Hat observation extent differs");
  Image image(25,20,1,type);
  for(std::size_t i=0;i<500;++i){std::uint32_t word=0;
    for(int j=0;j<bytes;++j)word|=std::stoul(raw.substr((i*bytes+j)*2,2),nullptr,16)<<(8*j);
    if(type==PixelType::Float32){float v;std::memcpy(&v,&word,4);image.pixels[i]=double(v);}else image.pixels[i]=word;
  }return image;
}
void references(const char *path){
  std::ifstream input(path);check(bool(input),"Missing Top Hat runner corpus");const auto corpus=Json::parse(input);
  check(corpus.at("cases").size()==576,"Top Hat matrix changed");
  check(corpus.at("imagej_commit")=="62db90a269cccc1ac79f658e4602043214ed055b","Wrong runner source");
  for(const auto &row:corpus.at("cases")){
    Stack planes;for(const auto &raw:row.at("input_hex"))planes.push_back(decode(row,raw.get<std::string>()));
    DocumentData data{Dataset::from_stack(planes)};
    if(row.at("selection")==1)data.selection=Selection("rectangle",{{2,3},{21,16}});
    if(row.at("selection")==2)data.selection=Selection("polygon",{{2,3},{20,2},{18,17},{5,15}});
    Document owner(std::move(data));auto display=owner.snapshot().display();display.crosshair={12,10,1};
    display.properties["display_range"]=metadata_array({7.,127.});
    display.properties["lut"]=std::string(row.at("inverted_lut").get<bool>()?"Inverted":"Grays");
    set_manual_threshold_state(display,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});
    owner.set_display(display);const auto before=owner.snapshot();
    TargetRequest request;if(row.at("mode")==1)request.scope=TargetScope::AllZ;
    const auto command=resolve_top_hat_command(before,request,{row.at("radius"),row.at("light_background"),row.at("dont_subtract")});
    check(std::string(command.operation())=="top_hat_imagej","Wrong operation intent");
    check(command.inverted_lut()==row.at("inverted_lut").get<bool>(),"Wrong captured LUT");
    check(metadata_equal(rank_command_from_record(before,command.record()).record(),command.record()),"Strict record did not round trip");
    auto prepared=prepare_operation(command.operation(),command.target(),command.parameters());
    const auto after=prepared.preview();
    for(int z=0;z<3;++z)check(after.data().dataset.plane({0,0,z})->pixels==decode(row,row.at("output_hex").at(z).get<std::string>()).pixels,"Command pixels differ");
    auto expected=display;const auto range=row.at("final_range").get<std::array<double,2>>();
    expected.properties["display_range"]=metadata_array({range[0],range[1]});
    if(row.at("final_threshold").is_null())reset_manual_threshold_state(expected);
    check(metadata_equal(after.display().properties,expected.properties),"Command final display differs");
    auto edit=prepared.begin_edit();owner.commit(edit);
    if(owner.history().undo_steps){owner.undo();check(metadata_equal(owner.snapshot().display().properties,before.display().properties),"Undo display mismatch");owner.redo();check(metadata_equal(owner.snapshot().display().properties,expected.properties),"Redo display mismatch");}

  }
}
struct Descriptor : PlaneSource {
 mutable int reads=0;
 DatasetShape shape() const override{return {1,1,1,4096,4096,1};}
 PixelType pixel_type() const override{return PixelType::UInt16;}
 std::shared_ptr<const Image> read_plane(PlanePosition) const override{++reads;throw std::runtime_error("Unexpected descriptor read");}
};
void workspace(){
 const OperationDefaultContext small{1,1,1,PixelType::UInt8,{}};
 const OperationParameters small_p{{"radius",0.0},{"light_background",false},{"dont_subtract",true},{"inverted_lut",false}};
 auto large_p=small_p;large_p["radius"]=1000.0;
 const auto low=operation_extra_workspace("top_hat_imagej",small,small_p);
 const auto high=operation_extra_workspace("top_hat_imagej",small,large_p);
 const auto tiny=rank_filter_kernel_imagej(0).half_widths.size();
 const auto big=rank_filter_kernel_imagej(1000).half_widths.size();
 check(high-low==2*(big-tiny)*sizeof(int),"Top Hat omitted its retained outer footprint");
 const auto image=operation_extra_workspace("top_hat_imagej",{2,3,1,PixelType::UInt8,{}},small_p);
 check(image-low==5*sizeof(double),"Top Hat first-pass image reservation differs");
 const auto short_background=operation_extra_workspace("top_hat_imagej",{1,1,1,PixelType::UInt16,{}},small_p);
 auto subtract=small_p;subtract["dont_subtract"]=false;
 const auto short_subtract=operation_extra_workspace("top_hat_imagej",{1,1,1,PixelType::UInt16,{}},subtract);
 check(short_subtract>short_background+1024*1024,"Top Hat final histogram not reserved");
}
void admission(){
 auto held=std::make_shared<Descriptor>();Document owner{Dataset(held)};
 const auto command=resolve_top_hat_command(owner.snapshot(),{}, {1,false,true});
 check(command.top_hat_options()->dont_subtract && held->reads==0,"Background-only command acquired samples");
 refuses([&]{resolve_top_hat_command(owner.snapshot(),{}, {1,false,false});});
 check(held->reads==0,"Rejected histogram admission read samples");
}
void staged_display_history(){
 for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32}) {
  Image image(5,5,1,type);
  auto owner=std::make_shared<Document>(Dataset::from_stack(Stack{image}));
  auto view=owner->snapshot().display();view.properties["display_range"]=metadata_array({7.,127.});
  set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),{{4.,8.},{0,0,0}});owner->set_display(view);
  const auto before=owner->snapshot();Application app;app.adopt(owner);
  TargetRequest request;request.destination=OutputDestination::NewDocument;
  const auto command=resolve_top_hat_command(before,request,{1,false,false});
  auto proposal=prepare_operation(command.operation(),command.target(),command.parameters());
  proposal.stage_new_document();const auto output=app.publish(proposal);const auto after=output.document->snapshot();
  check(!manual_threshold_state(after.display(),after.data().dataset.shape()),"Staged output did not clear threshold");
  output.document->undo();check(metadata_equal(output.document->snapshot().display().properties,before.display().properties),"Staged output Undo lost original display");
  output.document->redo();check(metadata_equal(output.document->snapshot().display().properties,after.display().properties),"Staged output Redo lost final display");
  check(owner->history().undo_steps==0 && metadata_equal(owner->snapshot().display().properties,before.display().properties),"Staged output changed source");
 }
}
void guards(){
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32}){
    Image image(5,5,1,type);image.pixels.assign(25,40);image.pixels[12]=100;
    const auto before=image.pixels;
    auto bright=top_hat_imagej(image,1);
    check(bright.pixels[12]==60 && bright.pixels[0]==0,"Opening residual incorrect");
    check(top_hat_imagej(image,1,false,true).pixels==std::vector<double>(25,40),"Opening background incorrect");
    image.pixels.assign(25,100);image.pixels[12]=40;
    auto dark=top_hat_imagej(image,1,true);
    const double offset=type==PixelType::UInt8?255:type==PixelType::UInt16?65535:0;
    check(dark.pixels[12]==offset-60 && dark.pixels[0]==offset,"Closing residual incorrect");
    check(top_hat_imagej(image,1,false,false,true).pixels==dark.pixels,"LUT inversion not XOR");
    check(top_hat_imagej(image,1,true,true).pixels==std::vector<double>(25,100),"Closing background incorrect");
    check(top_hat_imagej(image,1000,false,false,false,RankFilterExtent{0,0,0,0}).pixels==image.pixels,"Empty extent changed source");
    refuses([&]{top_hat_imagej(image,1000.1);});
    refuses([&]{top_hat_imagej(image,-1);});
    refuses([&]{top_hat_imagej(image,std::numeric_limits<double>::quiet_NaN());});
    refuses([&]{top_hat_imagej(image,1,false,false,false,RankFilterExtent{4,4,2,2});});
    check(top_hat_imagej(Image(1,1,1,type),101).pixels[0]==0,"Large radius failed");
  }
  refuses([]{top_hat_imagej(Image(3,3,1,PixelType::Float64));});
  refuses([]{top_hat_imagej(Image(3,3,3,PixelType::UInt8));});
  Image special(3,3,1,PixelType::Float32);
  special.pixels[0]=std::numeric_limits<double>::quiet_NaN();refuses([&]{top_hat_imagej(special);});
  special.pixels[0]=std::numeric_limits<double>::infinity();refuses([&]{top_hat_imagej(special);});
  special.pixels[0]=0.1;refuses([&]{top_hat_imagej(special);});
  special.pixels.assign(9,-double(std::numeric_limits<float>::max()));special.pixels[4]=std::numeric_limits<float>::max();
  refuses([&]{top_hat_imagej(special,1);});
  special.pixels.assign(9,0.);
  ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();
  bool cancelled=false;try{ExecutionScope scope(control);top_hat_imagej(special);}catch(const OperationCancelled&){cancelled=true;}
  check(cancelled,"Pre-cancelled Top Hat did work");
  bool progressed=false;control.cancellation=std::make_shared<CancellationToken>();
  control.stage_progress=[&](std::string_view name, std::size_t, std::size_t){if(name=="ImageJ top hat subtraction and boundary restore"){progressed=true;control.cancellation->cancel();}};
  cancelled=false;try{ExecutionScope scope(control);top_hat_imagej(Image(256,256,1,PixelType::UInt8));}catch(const OperationCancelled&){cancelled=true;}
  check(progressed && cancelled,"Top Hat finalization did not poll cancellation");
}

}
int main(int argc,char **argv){try{check(argc==2,"Pass Top Hat fixture");references(argv[1]);guards();admission();workspace();staged_display_history();std::cout<<checks<<" Top Hat command checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
