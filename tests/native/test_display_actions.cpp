#include "window.hpp"
#include "headless_session_internal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::HeadlessInputSession;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){
  try{action();}catch(const std::exception &){return;}
  throw std::runtime_error("Expected invalid display request to refuse");
}
struct Source final:PlaneSource {
  PixelType type;
  int samples;
  mutable int reads=0;
  Source(PixelType pixelType,int s):type(pixelType),samples(s){}
  DatasetShape shape()const override{return {1,1,2,4,5,samples};}
  PixelType pixel_type()const override{return type;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{
    ++reads;throw std::runtime_error("Metadata action requested source pixels");
  }
};
void write(const std::filesystem::path &path,const Json &value){std::ofstream(path)<<value.dump(2);}
Json read(const std::filesystem::path &path){Json value;std::ifstream(path)>>value;return value;}
void appendClick(Json &events,const Json &point){
  events.push_back({{"type","position"},{"space","screen"},{"point",point}});
  events.push_back({{"type","button"},{"name","left"},{"down",true}});
  events.push_back({{"type","button"},{"name","left"},{"down",false}});
}
void metadata(const std::filesystem::path &root){
  Json table=Json::array();for(int i=0;i<256;++i)table.push_back({i,255-i,(i*3)%256});
  const auto input=root/"palette.json",output=root/"saved.json";write(input,{{"format","imagejxx-lut"},{"lut",table}});
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64})for(int samples:{1,3,4}){
    auto source=std::make_shared<Source>(type,samples);
    Window w((root/(std::to_string(int(type))+"-"+std::to_string(samples)+".json")).u8string());
    w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
    const auto before=w.document()->nativeSnapshot();
    w.action("display_range",{{"minimum",2},{"maximum",123}});
    w.action("lut",{{"name","Fire"}});
    w.action("load_lut",{{"path",input.u8string()}});
    w.action("save_lut",{{"path",output.u8string()}});
    check(source->reads==0,"Virtual display configuration must perform zero provider reads");
    check(read(output).at("lut")==table,"Custom palette export preserves every RGB row");
    const auto after=w.document()->nativeSnapshot();
    check(before.shares_data(after) && before.revisions().pixels==after.revisions().pixels &&
      before.revisions().metadata==after.revisions().metadata && !after.dirty() && w.document()->undoSteps()==0,
      "Display-only commands preserve pixels, metadata history and dirty state");
    check(imagejxx::desktop::metadataJson(after.display().properties.at("display_range"))==Json::array({2,123}),"Manual range published from metadata");
    const auto stable=after.display();
    refuses([&]{w.action("display_range",{{"minimum",9},{"maximum",1}});});
    refuses([&]{w.action("display_range",{{"minimum",9},{"maximum",9}});});
    auto malformed=table;malformed[200]={0,999,0};write(input,{{"lut",malformed}});
    refuses([&]{w.action("load_lut",{{"path",input.u8string()}});});
    check(source->reads==0 && w.document()->nativeSnapshot().display().revision==stable.revision,
      "Invalid range and unused malformed palette row refuse before IO/publication");
    write(input,{{"lut",table}});
  }
}
void ieee(const std::filesystem::path &root){
  Image pixels(2,1,1,PixelType::Float32);
  pixels.pixels={float32_source_sample(0xff800000,FloatingSamplePolicy::PreserveIEEEFloat32),
                 float32_source_sample(0x7fc12345,FloatingSamplePolicy::PreserveIEEEFloat32)};
  auto observer=DatasetReadObserver();
  auto data=Dataset::from_stack({pixels},{},FloatingSamplePolicy::PreserveIEEEFloat32).observe_reads(observer);
  Window w((root/"ieee.json").u8string());w.addVirtualDocument(std::make_shared<Document>(data));
  const auto baseline=observer.counts().plane_reads;
  w.action("display_range",{{"minimum",0},{"maximum",0}});
  w.action("lut",{{"name","Red"}});
  check(observer.counts().plane_reads==baseline,"IEEE manual controls validate without reading live samples");
  const auto stable=w.document()->nativeSnapshot().display().revision;
  refuses([&]{w.action("display_range",{{"minimum",0},{"maximum",1e100}});});
  check(w.document()->nativeSnapshot().display().revision==stable && observer.counts().plane_reads==baseline,"IEEE binary32 endpoint refusal is atomic and metadata-only");
  w.action("display_range",{{"auto",true}});
  check(!w.document()->nativeSnapshot().display().properties.count("display_range") && observer.counts().plane_reads==baseline,"IEEE reset defers range reads to renderer");
}
void menu(const std::filesystem::path &root){
  auto source=std::make_shared<Source>(PixelType::UInt16,1);
  Window w((root/"menu.json").u8string());w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
  auto report=w.renderHeadless();
  check(source->reads==0,"Initial metadata canvas performs no reads");
  const auto imageMenu=report.at("host_controls").at("menu.Image").at("point");
  Json events=Json::array();appendClick(events,imageMenu);
  report=w.renderHeadless(3,false,false,events);
  check(report.at("host_controls").contains("menu.form.display_range"),"Image menu exposes actual display form control");
  check(source->reads==0,"Opening Image menu determines range defaults without provider IO");
  const auto form=report.at("host_controls").at("menu.form.display_range").at("point");
  events=Json::array();appendClick(events,imageMenu);appendClick(events,form);
  report=w.renderHeadless(3,false,false,events);
  check(report.at("dialog").at("values").at("maximum")==65535,"U16 menu defaults come from pixel-type metadata");
  check(source->reads==0,"Opening display-range form performs no reads");

}
} // namespace
int main(int argc,char **argv){try{
  check(argc==2,"Owned fixture path required");const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
  check(!std::filesystem::exists(root),"Fresh fixture root required");std::filesystem::create_directories(root);
  metadata(root);ieee(root);menu(root);
  std::cout<<"PASS metadata-only manual contrast, palette controls, IEEE validation and Image menu defaults\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
