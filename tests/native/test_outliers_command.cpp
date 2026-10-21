#include <imagejxx/rank_command.hpp>
#include "kernel_control.hpp"
#include "axis_command_record.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold.hpp>
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
template<class F> void refuses(F f){bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;}check(threw,"Invalid outlier input admitted");}
Image decode(const Json &row,const std::string &raw){
  const auto type=row.at("dtype")=="uint8"?PixelType::UInt8:row.at("dtype")=="uint16"?PixelType::UInt16:PixelType::Float32;
  const int bytes=type==PixelType::UInt8?1:type==PixelType::UInt16?2:4;
  check(raw.size()==std::size_t(500*bytes*2),"Outlier observation extent differs");
  Image image(25,20,1,type);
  for(std::size_t i=0;i<500;++i){std::uint32_t word=0;
    for(int j=0;j<bytes;++j)word|=std::stoul(raw.substr((i*bytes+j)*2,2),nullptr,16)<<(8*j);
    if(type==PixelType::Float32){float v;std::memcpy(&v,&word,4);image.pixels[i]=double(v);}else image.pixels[i]=word;
  }return image;
}

void references(const char *path){
  std::ifstream input(path);check(bool(input),"Missing runner corpus");const auto corpus=Json::parse(input);
  check(corpus.at("cases").size()==288,"Outlier command matrix changed");
  for(const auto &row:corpus.at("cases")){
    Stack planes;for(const auto &raw:row.at("input_hex"))planes.push_back(decode(row,raw.get<std::string>()));
    DatasetReadObserver reads;DocumentData data{Dataset::from_stack(planes).observe_reads(reads)};
    if(row.at("selection")==1)data.selection=Selection("rectangle",{{2,3},{21,16}});
    if(row.at("selection")==2)data.selection=Selection("polygon",{{2,3},{20,2},{18,17},{5,15}});
    Document owner(std::move(data));auto display=owner.snapshot().display();display.crosshair={12,10,1};
    display.properties["lut"]=std::string(row.at("inverted_lut").get<bool>()?"Inverted":"Grays");
    display.properties["display_range"]=metadata_array({7.,127.});
    set_manual_threshold_state(display,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});owner.set_display(display);
    const auto before=owner.snapshot();TargetRequest request;if(row.at("mode")==1)request.scope=TargetScope::AllZ;
    const auto command=resolve_remove_outliers_command(before,request,{row.at("radius"),row.at("threshold"),row.at("which")});
    check(reads.counts().plane_reads==0,"Outlier resolution read source");
    check(std::string(command.operation())=="remove_outliers_imagej" && command.outlier_options().has_value(),"Wrong outlier command identity");
    check(command.inverted_lut()==row.at("inverted_lut").get<bool>(),"Wrong captured LUT polarity");
    const auto replay=rank_command_from_record(before,command.record());check(metadata_equal(replay.record(),command.record()),"Outlier record changed on replay");
    const auto prepared=prepare_operation(command.operation(),command.target(),command.parameters());
    for(int z=0;z<3;++z)check(prepared.preview().data().dataset.plane({0,0,z})->pixels==decode(row,row.at("output_hex").at(z).get<std::string>()).pixels,"Outlier command differs from runner");
    check(metadata_equal(prepared.preview().display().properties,before.display().properties),"Outliers changed display/threshold");
    auto edit=prepared.begin_edit();owner.commit(edit);if(owner.history().undo_steps){owner.undo();for(int z=0;z<3;++z)check(owner.snapshot().data().dataset.plane({0,0,z})->pixels==planes[z].pixels,"Outlier undo changed source");owner.redo();}
  }
}
void lut_references(const char *path){
  std::ifstream input(path);check(bool(input),"Missing LUT corpus");const auto corpus=Json::parse(input);check(corpus.at("cases").size()==60,"LUT case count changed");
  for(const auto &row:corpus.at("cases"))for(int version=1;version<=(row.at("mode")==0?1:2);++version){
    Image plane(3,3,1,row.at("dtype")=="uint8"?PixelType::UInt8:row.at("dtype")=="uint16"?PixelType::UInt16:PixelType::Float32);plane.pixels=row.at("before").get<std::vector<double>>();
    const bool scientific=row.at("mode")!=0;const int c=scientific?row.at("channel").get<int>():0;
    DatasetReadObserver reads;Document owner(Dataset::from_planes({1,scientific?2:1,1,3,3,1},scientific?Stack{plane,plane}:Stack{plane}).observe_reads(reads));
    auto display=owner.snapshot().display();display.c=c;
    if(scientific){ChannelDisplayState state;state.mode=row.at("mode")==1?ChannelDisplayMode::Composite:row.at("mode")==2?ChannelDisplayMode::Color:ChannelDisplayMode::Grayscale;
      if(version==2){state.version=ChannelDisplayVersion::V2;state.projection=ChannelDisplayProjection::Max;}
      for(int k=0;k<2;k++){ChannelDisplayTable table{};for(int i=0;i<256;i++)table[i]={std::uint8_t(k?255-i:i),std::uint8_t(k?255-i:i),std::uint8_t(k?255-i:i)};state.channels.push_back({true,{0,255},{"",table}});}
      display=with_channel_display(display,owner.snapshot().data().dataset,state);
    }else{MetadataArray table;for(const auto &rgb:row.at("palette"))table.push_back(metadata_array({rgb.at(0).get<int>(),rgb.at(1).get<int>(),rgb.at(2).get<int>()}));display.properties["custom_lut"]=metadata_array(std::move(table));display.properties["lut"]=std::string("Inverted");}
    if(row.at("threshold")==1)set_manual_threshold_state(display,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,c,0}});
    owner.set_display(display);const auto before=owner.snapshot();const auto command=resolve_remove_outliers_command(before,{}, {1,50,"bright"});
    check(reads.counts().plane_reads==0,"LUT policy read source pixels");check(command.inverted_lut()==row.at("inverted").get<bool>(),"Active processor LUT policy differs from ImageJ");
    const auto result=prepare_operation(command.operation(),command.target(),command.parameters());
    check(result.preview().data().dataset.plane({0,c,0})->pixels==row.at("after").get<std::vector<double>>(),"LUT polarity changed outlier output");
    check(metadata_equal(result.preview().display().properties,before.display().properties),"LUT command changed display");
  }
}
void new_documents(){
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32}){
    Image image(5,5,1,type);image.pixels[12]=80;
    auto owner=std::make_shared<Document>(Dataset::from_stack(Stack{image}));Application app;app.adopt(owner);
    TargetRequest request;request.destination=OutputDestination::NewDocument;
    const auto command=resolve_remove_outliers_command(owner->snapshot(),request,{1,50,"bright"});
    auto proposal=prepare_operation(command.operation(),command.target(),command.parameters());
    const auto staged=proposal.stage_new_document();check(staged->snapshot().data().dataset.plane({})->pixels[12]==0,"Outlier new-document staging lost filtered pixels");
    check(owner->snapshot().data().dataset.plane({})->pixels[12]==80 && owner->history().undo_steps==0,"Outlier staging modified source");
    const auto result=app.publish(proposal);check(result.created && result.document_id!=owner->id,"Outlier output was not adopted");
    check(result.document->snapshot().data().dataset.plane({})->pixels[12]==0,"Adopted outlier output changed");
  }
}

void guards(){
  DatasetReadObserver reads;Document owner(Dataset::from_stack(Stack{Image(3,3,1,PixelType::UInt8)}).observe_reads(reads));
  refuses([&]{resolve_remove_outliers_command(owner.snapshot(),{},{101,50,"bright"});});
  refuses([&]{resolve_remove_outliers_command(owner.snapshot(),{},{1,-1,"bright"});});
  refuses([&]{resolve_remove_outliers_command(owner.snapshot(),{},{1,50,"Both"});});
  auto view=owner.snapshot().display();view.properties["lut"]=std::string("unknown");owner.set_display(view);refuses([&]{resolve_remove_outliers_command(owner.snapshot());});
  check(reads.counts().plane_reads==0,"Rejected outlier metadata read pixels");
  view.properties["lut"]=std::string("Inverted");owner.set_display(view);const auto command=resolve_remove_outliers_command(owner.snapshot());
  Document destination(Dataset::from_stack(Stack{Image(3,3,1,PixelType::UInt8)}));
  check(rebind_rank_command(destination.snapshot(),command.record(),{}).inverted_lut(),"Explicit rebind lost frozen numeric direction");
  auto record=command.record();const auto options=detail::axis_command_record::object_value(record.at("options"));
  for(int kind=0;kind<4;kind++){auto bad=options;if(kind==0)bad["inverted_lut"]=std::int64_t(1);if(kind==1)bad.erase("which");if(kind==2)bad["method"]=std::string("median");if(kind==3)bad["threshold"]=-1.;record["options"]=metadata_object(bad);refuses([&]{validate_rank_command_record(record);});}
  auto legacy=resolve_rank_command(owner.snapshot()).record();auto legacyOptions=detail::axis_command_record::object_value(legacy.at("options"));legacyOptions["threshold"]=50.;legacy["options"]=metadata_object(legacyOptions);refuses([&]{validate_rank_command_record(legacy);});
  ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();bool cancelled=false;try{prepare_operation(command.operation(),command.target(),command.parameters(),{},control);}catch(const OperationCancelled&){cancelled=true;}check(cancelled && reads.counts().plane_reads==0,"Pre-cancelled outlier command read/published");
}
}
int main(int argc,char **argv){try{check(argc==3,"Pass pixel and LUT fixtures");references(argv[1]);lut_references(argv[2]);new_documents();guards();std::cout<<checks<<" outlier command checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
