#include "window.hpp"
#include "headless_session_internal.hpp"
#include <filesystem>
#include <iostream>
#include <cstdlib>
using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Source final:PlaneSource {
  mutable int reads=0;bool fail=false;
  DatasetShape shape()const override{return {1,1,2,256,256,1};}
  PixelType pixel_type()const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override {
    ++reads;if(fail)throw std::runtime_error("Deferred pixel failure");
    auto image=std::make_shared<Image>(256,256,1,PixelType::UInt16);
    for(int y=0;y<256;++y)for(int x=0;x<256;++x)image->at(x,y)=(x+256*y+100*p.z)%65536;
    return image;
  }
};
void attach(Window &w,const std::shared_ptr<Source> &source){for(int i=0;i<3;++i)w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));}
int main(int argc,char **argv){try{
  check(argc==2,"Owned fixture path required");namespace fs=std::filesystem;
  auto root=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(root),"Fresh fixture required");fs::create_directories(root);
  auto source=std::make_shared<Source>();source->fail=true;
  Window metadata((root/"metadata.json").u8string());metadata.updateSettings({{"viewport_cache_mb",1}},false);attach(metadata,source);
  auto report=metadata.renderHeadless(8);
  check(source->reads==0 && metadata.virtualViewCacheInfo().at("entries")==0,"Geometry/properties must not read even failing providers");
  check(report.at("document_info").at("axis_sizes").at("X")==256,"Metadata geometry remains available");
  source->fail=false;
  Window capture((root/"capture.json").u8string());capture.updateSettings({{"viewport_cache_mb",1}},false);attach(capture,source);
  {
    imagejxx::desktop::HeadlessInputSession session(capture);
    for(int i=0;i<3;++i){capture.setActiveIndex(i);report=session.frame();report=session.frame();}
    check(source->reads>=3,"Actual render must acquire visited tabs");
    // Force eviction of the active tab after its RGB/readback is current.
    capture.documents()[0]->sharedFrame();capture.documents()[1]->sharedFrame();
    auto baseline=source->reads;
    for(int i=0;i<6;++i)report=session.frame();
    check(source->reads==baseline,"Stable captured RGB must survive CPU cache eviction");
    check(capture.virtualViewCacheInfo().at("retained_bytes")==1048576,"Cache pressure fixture must evict one plane");
    auto pixels=report.at("ordinary_pixels");
    check(!pixels.is_null(),"Actual rendered diagnostics required");
    auto origin=report.at("ordinary_canvas").at("origin");auto scale=report.at("ordinary_canvas").at("scale");
    const Json point=Json::array({origin[0].get<double>()+4.5*scale[0].get<double>(),origin[1].get<double>()+3.5*scale[1].get<double>()});
    report=session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));
    check(source->reads==baseline+1,"Deferred hover must reacquire only its evicted source plane");
    check(report.at("status").get<std::string>().find("772")!=std::string::npos,"Deferred hover reads the displayed source sample");
  }
  if(std::getenv("IMAGEJXX_RENDER_TESTS")){
    auto glSource=std::make_shared<Source>();Window gl((root/"gl.json").u8string());gl.updateSettings({{"viewport_cache_mb",1}},false);attach(gl,glSource);
    imagejxx::desktop::RunOptions options;options.hidden=true;options.frames=8;
    check(gl.run(options)==0,"Hidden OpenGL run failed");
    check(glSource->reads==1,"Unchanged GL texture must acquire its source only once");
    gl.documents()[0]->sharedFrame();gl.documents()[1]->sharedFrame();
    auto before=glSource->reads;
    check(gl.run(options)==0 && glSource->reads==before+1,"A recreated GL context must rebuild from the evicted active plane");
    check(gl.virtualViewCacheInfo().at("retained_bytes")<=1048576,"GL draw must preserve CPU retention budget");
  }
  std::cout<<"PASS canvas metadata, stable readback/texture reuse under cache pressure and deferred sampling\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
