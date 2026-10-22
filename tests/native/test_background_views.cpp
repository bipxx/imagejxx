#include "window.hpp"
#include "rendering.hpp"
#include <imagejxx/float32_display.hpp>
#include <algorithm>
#include "viewport_renderer.hpp"
#include "headless_session_internal.hpp"
#include "../../src/native/kernel_control.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>

using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::HeadlessInputSession;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void until(F predicate) {
  const auto end = std::chrono::steady_clock::now()+5s;
  while (!predicate()) {
    check(std::chrono::steady_clock::now()<end,"Background view deadline");
    std::this_thread::sleep_for(2ms);
  }
}
struct Source final : PlaneSource {
  const std::thread::id creator=std::this_thread::get_id();
  mutable std::atomic<int> mainReads{0},workerReads{0},entered{0};
  mutable std::mutex mutex;
  mutable std::condition_variable changed;
  bool released=false;
  std::atomic<bool> fail{false};
  DatasetShape shape() const override {return {2,2,4,6,8,1};}
  PixelType pixel_type() const override {return PixelType::UInt16;}
  static double value(PlanePosition p,int x,int y) {return p.t*80+p.c*40+p.z*10+y*8+x;}
  void open() {std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    if(std::this_thread::get_id()==creator)++mainReads;else ++workerReads;
    ++entered;
    const auto end=std::chrono::steady_clock::now()+5s;
    std::unique_lock<std::mutex> lock(mutex);
    while(!released){
      lock.unlock();kernel_checkpoint();lock.lock();
      check(std::chrono::steady_clock::now()<end,"Source read blocked the UI");
      changed.wait_for(lock,1ms);
    }
    lock.unlock();
    if(fail)throw std::runtime_error("Background source failure");
    auto image=std::make_shared<Image>(8,6,1,PixelType::UInt16);
    for(int y=0;y<6;++y)for(int x=0;x<8;++x)image->at(x,y)=value(p,x,y);
    return image;
  }
};
struct OpenOnExit {std::shared_ptr<Source> source;~OpenOnExit(){source->open();}};
bool ready(const Json &report,bool xyz) {
  if(!xyz)return !report.at("ordinary_canvas").at("loading").get<bool>() &&
    report.at("ordinary_pixels").at("readback").at("available").get<bool>();
  const auto &planes=report.at("orthogonal_planes");
  return planes.size()==3 && std::all_of(planes.begin(),planes.end(),[](const Json &p){return !p.at("loading").get<bool>() && p.at("readback").at("available").get<bool>();});
}
void values(const Json &report,bool xyz,PlanePosition p,std::array<int,3> cross) {
  Json planes=xyz?report.at("orthogonal_planes"):Json::array({report.at("ordinary_pixels")});
  for(std::size_t i=0;i<planes.size();++i){
    const auto &rb=planes[i].at("readback");
    check(rb.at("source_tcz")==Json::array({p.t,p.c,p.z}),"Published latest captured TCZ");
    const auto &source=rb.at("source");const auto &rgb=rb.at("rendered_rgb");
    const int width=i==2?6:8,height=i==0?6:4;
    check(source.at("shape")==Json::array({height,width,1}) && rgb.at("shape")==Json::array({height,width,3}),"Orientation output geometry");
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
      const auto sourceZ=i==0?p.z:y;
      const auto sourceX=i==2?cross[0]:x,sourceY=i==0?y:i==1?cross[1]:x;
      const auto expected=Source::value({p.t,p.c,sourceZ},sourceX,sourceY);
      check(source.at("pixels")[y*width+x]==expected,"Independent source orientation formula");
      for(int c=0;c<3;++c)check(rgb.at("pixels")[(y*width+x)*3+c]==expected,"Independent grayscale mapping formula");
    }
  }
}
void responsive(const std::filesystem::path &root,bool xyz) {
  auto source=std::make_shared<Source>();
  Window window((root/(xyz?"xyz.json":"xy.json")).u8string());
  OpenOnExit release{source};
  window.updateSettings({{"async_viewport",true}},false);
  window.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
  auto display=window.document()->nativeSnapshot().display();
  display.properties["display_range"]=metadata_array({0.0,255.0});
  window.document()->setNativeDisplay(std::move(display));
  window.document()->setView({{"orthogonal",xyz}});
  {
    HeadlessInputSession session(window);
    Json report=session.frame();report=session.frame();
    until([&]{return source->entered>0;});
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<5;++i)report=session.frame();
    check(std::chrono::steady_clock::now()-start<1s,"Frames continue while provider is blocked");
    check(!ready(report,xyz) && source->mainReads==0,"Loading never synchronously reads source");
    window.document()->setView({{"t",1},{"c",1},{"crosshair",{2,3,2}}});
    report=session.frame();
    check(!ready(report,xyz),"Changed position cannot expose stale readback");
    check(report.at("viewport_jobs").at("discarded").get<int>()>0,"Position supersedes pending work");
    const auto &canvas=xyz?report.at("orthogonal_planes")[0]:report.at("ordinary_canvas");
    const auto origin=canvas.at("origin"),scale=canvas.at("scale");
    const Json focusPoint=xyz?canvas.at("canvas_point"):Json::array({origin[0].get<double>()+4*scale[0].get<double>(),origin[1].get<double>()+3*scale[1].get<double>()});
    report=session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",focusPoint}},{{"type","button"},{"name","left"},{"down",true}}}));
    report=session.frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    // Ordinary slices use wheel navigation; linked canvases also own page keys.
    if(xyz){
      report=session.frame(Json::array({{{"type","key"},{"name","PageUp"},{"down",true}}}));
      report=session.frame(Json::array({{{"type","key"},{"name","PageUp"},{"down",false}}}));
    }else report=session.frame(Json::array({{{"type","wheel"},{"x",0},{"y",1}}}));
    const auto final=window.document()->position();
    check(final.z==3,"Pending canvas slice navigation");
    source->open();
    until([&]{report=session.frame();return ready(report,xyz);});
    values(report,xyz,final,window.document()->view().crosshair);
    const auto reads=source->workerReads.load();
    for(int i=0;i<5;++i)report=session.frame();
    check(source->workerReads==reads && source->mainReads==0,"Stable background views reuse published pixels");
    if(!xyz){
      const auto origin=report.at("ordinary_canvas").at("origin"),scale=report.at("ordinary_canvas").at("scale");
      const Json point={origin[0].get<double>()+2.5*scale[0].get<double>(),origin[1].get<double>()+1.5*scale[1].get<double>()};
      report=session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));
      check(report.at("status").get<std::string>().find("160")!=std::string::npos,"Hover samples completed captured source");
      check(source->mainReads==0 && source->workerReads==reads,"Hover never falls back to IO");
    }
    if(!xyz){
      const auto submitted=report.at("viewport_jobs").at("submitted").get<int>();
      window.updateSettings({{"async_viewport",false}},false);report=session.frame();
      window.updateSettings({{"async_viewport",true}},false);report=session.frame();
      until([&]{report=session.frame();return ready(report,false);});
      check(report.at("viewport_jobs").at("submitted").get<int>()>submitted,"Enabling on a cached texture acquires a matching worker sample");
      values(report,false,final,window.document()->view().crosshair);
    }
    window.closeDocument(window.document()->id,true);
    until([&]{report=session.frame();return window.renderMemoryInfo().at("working_bytes")==0;});
    check(window.renderMemoryInfo().at("retained_bytes")==0,"Document close releases surfaces");
  }
}
void cancelLifecycle(const std::filesystem::path &root) {
  auto source=std::make_shared<Source>();Window window((root/"cancel.json").u8string());OpenOnExit release{source};
  window.updateSettings({{"async_viewport",true}},false);
  window.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
  {
    HeadlessInputSession session(window);session.frame();session.frame();until([&]{return source->entered>0;});
    window.document()->setView({{"orthogonal",true}});auto report=session.frame();
    check(report.at("viewport_jobs").at("discarded").get<int>()>0,"Mode switch cancels ordinary worker");
    session.close();
  }
  source->open();
  {
    HeadlessInputSession session(window);
    window.closeDocument(window.document()->id,true);
    until([&]{session.frame();return window.renderMemoryInfo().at("working_bytes")==0;});
    check(source->mainReads==0 && window.renderMemoryInfo().at("retained_bytes")==0,"Context teardown cancels all view requests");
  }
}
void capturedRendering() {
  using imagejxx::desktop::ViewportRender;
  using imagejxx::desktop::renderViewport;
  Stack planes;
  for(int c=0;c<2;++c)for(int z=0;z<3;++z){
    Image image(2,2,1,PixelType::UInt16);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)image.at(x,y)=c*40+z*10+y*2+x;
    planes.push_back(std::move(image));
  }
  Document document(Dataset::from_planes({1,2,3,2,2,1},std::move(planes)));
  ChannelDisplayState channels;channels.channels.resize(2);
  channels.channels[0].lut.name="Red";channels.channels[1].lut.name="Green";
  document.set_display(with_channel_display(document.snapshot().display(),document.snapshot().data().dataset,channels));
  const auto live=document.snapshot();
  auto edit=document.begin_edit();Image changed(2,2,1,PixelType::UInt16);
  for(auto &sample:changed.pixels)sample=200;
  edit.replace_plane({0,1,1},std::move(changed));
  const auto preview=edit.preview();TargetRequest target;target.scope=TargetScope::Positions;target.roi=RoiPolicy::WholePlane;target.positions={{0,1,1}};
  ViewportRender request{live};request.preview=preview;request.target=resolve_target(live,target);request.previewGeneration=7;
  request.position={0,0,1};request.capture=true;request.linked=true;
  for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ}){
    request.plane=plane;request.coordinate=plane==Plane::XY?1:0;
    const auto result=renderViewport(request);const auto &rgb=*result.rgb;
    for(int y=0;y<rgb.height;++y)for(int x=0;x<rgb.width;++x){
      const auto z=plane==Plane::XY?1:y;
      const auto sourceX=plane==Plane::YZ?0:x,sourceY=plane==Plane::XY?y:plane==Plane::XZ?0:x;
      const double red=z*10+sourceY*2+sourceX,green=z==1?200:40+red;
      check(rgb.at(x,y,0)==red && rgb.at(x,y,1)==green && rgb.at(x,y,2)==0,"Captured channel preview independent projection");
      check(result.source->at(x,y)==red,"Active scalar stays live when another channel previews");
    }
    check(result.channels.at("contributors")[1].at("generation")==7,"Captured contributor diagnostics");
  }
  request.position.z=2;request.plane=Plane::XY;request.coordinate=2;
  const auto outside=renderViewport(request);
  check(outside.rgb->at(0,0,1)==60,"Preview target gate excludes navigation outside captured Z");

  for(auto policy:{FloatingSamplePolicy::FiniteOnly,FloatingSamplePolicy::PreserveQuietNaNFloat32,FloatingSamplePolicy::PreserveIEEEFloat32}){
    const auto sample=[&](std::uint32_t bits){return float32_source_sample(bits,policy);};
    Image image(3,2,1,PixelType::Float32);
    image.pixels={-2,-1,0,1,2,3};
    if(policy!=FloatingSamplePolicy::FiniteOnly)image.pixels[5]=sample(0x7fc12345);
    if(policy==FloatingSamplePolicy::PreserveIEEEFloat32){image.pixels[0]=sample(0xff800000);image.pixels[4]=sample(0x7f800000);}
    Document floating(Dataset::from_stack({image},{},policy));
    ViewportRender scalar{floating.snapshot()};scalar.metadata=Json::object();
    auto result=renderViewport(scalar);
    auto expected=imagejxx::desktop::renderImage(image,Json::object(),true,policy);
    check(result.rgb->pixels==expected.pixels,"Ordinary floating and IEEE rendering retains established mapping");
    if(policy!=FloatingSamplePolicy::FiniteOnly)
      check(float32_source_sample_bits(result.source->pixels[5],policy)==0x7fc12345,"Source NaN payload retained");
    scalar.linked=true;
    auto linked=renderViewport(scalar);
    check(imagejxx::desktop::sameDisplayRange(result.range,linked.range),"Scalar ordinary/linked automatic ranges agree");
    scalar.metadata["display_range"]={-1,1};
    auto manual=renderViewport(scalar);
    check(manual.range==std::array<double,2>{-1,1},"Manual range remains captured");
    check(manual.rgb->pixels==imagejxx::desktop::displayImage(image,scalar.metadata,policy,
       policy==FloatingSamplePolicy::PreserveIEEEFloat32?std::optional<std::array<std::uint32_t,2>>(imagejxx::desktop::displayRangeBits({-1,1})):std::nullopt).pixels,"Linked manual range mapping");
  }
}
void failureAndGl(const std::filesystem::path &root) {
  auto source=std::make_shared<Source>();source->open();source->fail=true;
  Window failed((root/"failure.json").u8string());failed.updateSettings({{"async_viewport",true}},false);
  failed.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
  {
    HeadlessInputSession session(failed);Json report;
    until([&]{report=session.frame();return !report.at("viewport_jobs").is_null() && report.at("viewport_jobs").at("failed").get<int>()==1;});
    check(!report.at("ordinary_pixels").at("readback").at("available").get<bool>(),"Failed reader exposes no stale readback");
    const auto reads=source->workerReads.load();for(int i=0;i<4;++i)session.frame();
    check(source->mainReads==0 && source->workerReads==reads,"Failed view key does not repeatedly decode");
  }
  if(std::getenv("IMAGEJXX_RENDER_TESTS"))for(bool xyz:{false,true}){
    auto data=std::make_shared<Source>();data->open();Window window((root/(xyz?"gl-xyz.json":"gl-xy.json")).u8string());
    window.updateSettings({{"async_viewport",true}},false);window.addVirtualDocument(std::make_shared<Document>(Dataset(data)));
    window.document()->setView({{"orthogonal",xyz}});
    imagejxx::desktop::RunOptions options;options.hidden=true;options.frames=30;
    check(window.run(options)==0 && data->mainReads==0 && data->workerReads>0,"GL ordinary/XYZ background read threads");
    auto reads=data->workerReads.load();
    check(window.run(options)==0 && data->mainReads==0 && data->workerReads>reads,"Recreated GL context rebuilds background view");
    check(window.renderMemoryInfo().at("retained_bytes")==0,"GL cleanup releases texture and sample reservations");
  }
}
} // namespace
int main(int argc,char **argv){try{
  check(argc==2,"Owned fixture root required");const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
  check(!std::filesystem::exists(root),"Fresh fixture root required");std::filesystem::create_directories(root);
  responsive(root,false);responsive(root,true);cancelLifecycle(root);capturedRendering();failureAndGl(root);
  std::cout<<"PASS ordinary/XYZ background loading, navigation, latest-only publication, independent pixels, hover and lifecycle\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
