#include <imagejxx/rank_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
std::size_t checks=0;
void check(bool v,const char *message){++checks;if(!v)throw std::runtime_error(message);}
Image decode(const Json &row,const std::string &raw) {
  const auto type=row.at("dtype")=="uint8"?PixelType::UInt8:row.at("dtype")=="uint16"?PixelType::UInt16:PixelType::Float32;
  const int bytes=type==PixelType::UInt8?1:type==PixelType::UInt16?2:4;
  check(raw.size()==std::size_t(500*bytes*2),"Despeckle observation extent differs");
  Image image(25,20,1,type);
  for(std::size_t i=0;i<500;++i){std::uint32_t word=0;
    for(int j=0;j<bytes;++j)word|=std::stoul(raw.substr((i*bytes+j)*2,2),nullptr,16)<<(8*j);
    if(type==PixelType::Float32){float value;std::memcpy(&value,&word,4);image.pixels[i]=double(value);}else image.pixels[i]=word;
  }return image;
}
void references(const char *path) {
  std::ifstream input(path);check(bool(input),"Missing Despeckle runner corpus");const auto corpus=Json::parse(input);
  check(corpus.at("cases").size()==72,"Despeckle matrix changed");
  check(corpus.at("imagej_commit")=="62db90a269cccc1ac79f658e4602043214ed055b","Wrong runner source");
  for(const auto &row:corpus.at("cases")){
    Stack planes;for(const auto &raw:row.at("input_hex"))planes.push_back(decode(row,raw.get<std::string>()));
    DatasetReadObserver reads;DocumentData data{Dataset::from_stack(planes).observe_reads(reads)};
    if(row.at("selection")==1)data.selection=Selection("rectangle",{{2,3},{21,16}});
    if(row.at("selection")==2)data.selection=Selection("polygon",{{2,3},{20,2},{18,17},{5,15}});
    Document owner(std::move(data));auto display=owner.snapshot().display();display.crosshair={12,10,1};
    display.properties["display_range"]=metadata_array({7.,127.});
    set_manual_threshold_state(display,owner.snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});
    owner.set_display(display);const auto before=owner.snapshot();
    TargetRequest request;if(row.at("mode")==1)request.scope=TargetScope::AllZ;
    const auto command=resolve_despeckle_command(before,request);
    check(reads.counts().plane_reads==0,"Despeckle resolution read samples");
    check(command.options().method==RankFilterMethod::Median && command.options().radius==1.,"Despeckle preset is not fixed median1");
    const auto replay=rank_command_from_record(before,command.record());
    check(metadata_equal(replay.record(),command.record()),"Canonical Despeckle record did not replay");
    const auto prepared=prepare_operation("rank_filter_imagej",command.target(),command.parameters());
    for(int z=0;z<3;++z)check(prepared.preview().data().dataset.plane({0,0,z})->pixels==decode(row,row.at("output_hex").at(z).get<std::string>()).pixels,"Despeckle pixels differ from runner");
    check(metadata_equal(prepared.preview().display().properties,before.display().properties),"Despeckle changed source display/threshold");
    if(row.at("selection")!=2){
      const auto extent=row.at("selection")==1?std::optional<RankFilterExtent>{{2,3,19,13}}:std::nullopt;
      check(despeckle_imagej(planes[1],extent).pixels==decode(row,row.at("output_hex").at(1).get<std::string>()).pixels,"Direct fixed Despeckle differs");
    }
    auto edit=prepared.begin_edit();owner.commit(edit);
    if(owner.history().undo_steps){owner.undo();for(int z=0;z<3;++z)check(owner.snapshot().data().dataset.plane({0,0,z})->pixels==planes[z].pixels,"Despeckle undo lost source");owner.redo();}
    check(metadata_equal(owner.snapshot().display().properties,display.properties),"Despeckle history changed display");
  }
}
void guards() {
  Image sample(3,3,1,PixelType::UInt8);sample.pixels={0,100,0,100,0,100,0,100,0};
  check(despeckle_imagej(sample).pixels[4]==0,"Despeckle used a five-point disk instead of nine-point median");
  check(despeckle_imagej(sample,RankFilterExtent{0,0,0,0}).pixels==sample.pixels,"Empty extent changed pixels");
  bool refused=false;try{(void)despeckle_imagej(Image(3,3,1,PixelType::Float64));}catch(const std::invalid_argument&){refused=true;}
  check(refused,"Unqualified Float64 Despeckle admitted");
  DatasetReadObserver reads;Document owner(Dataset::from_stack(Stack{sample}).observe_reads(reads));
  const auto command=resolve_despeckle_command(owner.snapshot());ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();
  refused=false;try{(void)prepare_operation("rank_filter_imagej",command.target(),command.parameters(),{},control);}catch(const OperationCancelled&){refused=true;}
  check(refused && reads.counts().plane_reads==0 && owner.history().undo_steps==0,"Pre-cancelled Despeckle read/published");
}
}
int main(int argc,char **argv){try{check(argc==2,"Pass the Despeckle fixture");references(argv[1]);guards();std::cout<<checks<<" Despeckle checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
