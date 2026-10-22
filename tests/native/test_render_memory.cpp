#include "window.hpp"
#include "render_memory.hpp"
#include "headless_session_internal.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
#include <cstdlib>
using namespace imagejxx;
using imagejxx::desktop::RenderMemory;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){bool bad=false;try{f();}catch(const std::exception &){bad=true;}check(bad,"Expected resource refusal");}
struct Source:PlaneSource {
  int side=512;mutable int reads=0,regions=0;std::function<void()> onRead;
  DatasetShape shape()const override{return {1,1,3,side,side,1};}
  PixelType pixel_type()const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override {
    ++reads;if(onRead)onRead();auto out=std::make_shared<Image>(side,side,1,PixelType::UInt16);
    for(int y=0;y<side;++y)for(int x=0;x<side;++x)out->at(x,y)=(p.z*1000+y*side+x)%65536;return out;
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height)const override {
    ++regions;Image out(width,height,1,PixelType::UInt16);
    for(int j=0;j<height;++j)for(int i=0;i<width;++i)out.at(i,j)=(p.z*1000+(y+j)*side+x+i)%65536;return out;
  }
};
int main(int argc,char **argv){try{
  RenderMemory pool(100);auto a=pool.acquire(60);auto retained=a.retain(40);
  check(pool.working()==20 && pool.retained()==40 && pool.peak()==60,"Split reservation");
  refuses([&]{pool.acquire(41);});check(pool.denied()==1 && pool.used()==60,"Aggregate admission");
  refuses([&]{pool.setLimit(59);});check(pool.limit()==100,"Atomic lower limit");
  a.reset();check(pool.used()==40,"Working release");auto moved=std::move(retained);check(pool.used()==40,"Move transfer");
  moved.reset();pool.setLimit(20);check(pool.used()==0 && pool.limit()==20,"Retained release");
  refuses([&]{RenderMemory::mul(std::size_t(-1),2);});
  const auto planned=RenderMemory::plan(2,3,1,true,true,true);
  check(planned.texture==24 && planned.readback==192 && planned.rank==66 && planned.peak==540,"Declared surface arithmetic");
  check(argc==2,"Owned fixture path required");namespace fs=std::filesystem;
  const auto root=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(root),"Fresh fixture");fs::create_directories(root);
  for(bool xyz:{false,true}){
    auto source=std::make_shared<Source>();Window denied((root/(xyz?"denied-xyz.json":"denied-xy.json")).u8string());
    denied.updateSettings({{"render_memory_mb",1}},false);denied.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
    if(xyz)denied.document()->setView({{"orthogonal",true}});
    {imagejxx::desktop::HeadlessInputSession session(denied);session.frame();session.frame();}
    auto info=denied.renderMemoryInfo();check(source->reads==0 && source->regions==0 && info.at("retained_bytes")==0 && info.at("working_bytes")==0 && info.at("denied_requests").get<int>()>0,"Render rejection precedes provider IO");
    Window admitted((root/(xyz?"admitted-xyz.json":"admitted-xy.json")).u8string());
    admitted.updateSettings({{"render_memory_mb",32}},false);admitted.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
    if(xyz)admitted.document()->setView({{"orthogonal",true}});
    {
      imagejxx::desktop::HeadlessInputSession session(admitted);session.frame();session.frame();
      auto live=admitted.renderMemoryInfo();const auto expected=xyz?8486912:8388608;
      check(live.at("retained_bytes")==expected && live.at("working_bytes")==0,"Actual retained XY/XYZ capacity accounting");
      check(live.at("peak_reserved_bytes").get<std::size_t>()<=32*1048576,"Aggregate peak bound");
      refuses([&]{admitted.updateSettings({{"render_memory_mb",1}},false);});
      check(admitted.renderMemoryInfo()==live && admitted.settings().integer("render_memory_mb")==32,"Live-budget preference refusal is atomic");
      session.frame();check(admitted.renderMemoryInfo().at("retained_bytes")==expected,"Stable render does not accumulate leases");
    }
    check(admitted.renderMemoryInfo().at("retained_bytes")==0 && admitted.renderMemoryInfo().at("working_bytes")==0,"Context teardown releases diagnostic reservations");
  }
  if(std::getenv("IMAGEJXX_RENDER_TESTS")){
    auto source=std::make_shared<Source>();source->side=128;Window gl((root/"gl.json").u8string());gl.updateSettings({{"render_memory_mb",1}},false);gl.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
    bool reserved=false;source->onRead=[&]{reserved=gl.renderMemoryInfo().at("working_bytes").get<int>()>0;};
    imagejxx::desktop::RunOptions options;options.hidden=true;options.frames=4;
    check(gl.run(options)==0 && reserved,"GL buffers reserved before pixel IO");
    check(gl.renderMemoryInfo().at("retained_bytes")==0 && gl.renderMemoryInfo().at("working_bytes")==0,"GL teardown releases logical textures");
    check(gl.renderMemoryInfo().at("peak_reserved_bytes").get<std::size_t>()<=1048576,"GL peak limit");
  }
  std::cout<<"PASS shared render reservations, pre-IO refusal, XY/XYZ retained capacity, atomic settings and release\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
