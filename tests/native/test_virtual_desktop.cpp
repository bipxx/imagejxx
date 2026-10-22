#include "window.hpp"
#include "histogram_async_ui_probe.hpp"
#include <atomic>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
namespace {
void check(bool v){if(!v)throw std::runtime_error("Virtual desktop contract failed");}
template<class F>void refuses(F f){bool bad=false;try{f();}catch(const std::exception &){bad=true;}check(bad);}
struct Source:PlaneSource {
  DatasetShape sizes{2,2,3,2,4,1};
  std::thread::id creator=std::this_thread::get_id();
  mutable std::atomic<int> main_reads{0},worker_reads{0},region_reads{0};
  std::atomic<bool> fail{false};
  DatasetShape shape()const override{return sizes;}
  PixelType pixel_type()const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    if(std::this_thread::get_id()==creator)++main_reads;else ++worker_reads;
    if(fail)throw std::runtime_error("Virtual fixture read failure");
    auto out=std::make_shared<Image>(sizes.x,sizes.y,1,PixelType::UInt16);
    for(int i=0;i<sizes.x*sizes.y;++i)out->pixels[i]=100*p.t+40*p.c+8*p.z+i;
    return out;
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height)const override{
    ++region_reads;Image out(width,height,1,PixelType::UInt16);
    for(int j=0;j<height;++j)for(int i=0;i<width;++i)out.at(i,j)=100*p.t+40*p.c+8*p.z+(y+j)*sizes.x+x+i;
    return out;
  }
};
}
int main(int argc,char **argv){try{
  check(argc==2);namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(root));fs::create_directories(root);
  auto provider=std::make_shared<Source>();auto owner=std::make_shared<Document>(Dataset(provider));
  Window w((root/"prefs.json").u8string());
  refuses([&]{w.addNativeDocument(owner);});check(w.documents().empty() && provider->main_reads==0);
  refuses([&]{w.addVirtualDocument(owner,0);});check(w.documents().empty());
  w.addVirtualDocument(owner,128);auto facade=w.document();
  check(provider->main_reads==0 && provider->worker_reads==0 && facade->scientific());
  check(facade->state().frames.empty() && facade->shape().t==2 && facade->shape().c==2 && facade->shape().z==3);
  check(w.documentInfo().at("axis_sizes").at("Z")==3 && provider->main_reads==0);refuses([&]{facade->requireLegacyEditable();});
  auto first=facade->sharedFrame();check(first->at(3,1)==7 && provider->main_reads==1);
  check(facade->sharedFrame()==first && provider->main_reads==1);
  auto metadata=owner->begin_edit();metadata.set_property("note","metadata only");owner->commit(metadata);
  check(facade->state().frames.empty() && facade->sharedFrame()==first && provider->main_reads==1);
  facade->setView({{"t",1},{"c",1},{"crosshair",{0,0,2}}});check(provider->main_reads==1);
  auto next=facade->sharedFrame();check(next->at(3,1)==163 && first->at(3,1)==7 && provider->main_reads==2);
  check(facade->sharedFrame()==next && provider->main_reads==2);
  auto xz=facade->plane(Plane::XZ,1);check(xz.width==4 && xz.height==3 && xz.at(3,2)==163 && provider->region_reads==3);
  auto yz=facade->plane(Plane::YZ,3);check(yz.width==2 && yz.height==3 && yz.at(1,2)==163 && provider->region_reads==6);
  check(provider->main_reads==2);auto worker=facade->snapshot();check(worker.state().frames.empty());
  auto wrong_thread=std::async(std::launch::async,[owner]{try{owner->set_display(owner->snapshot().display());return false;}catch(const std::runtime_error &){return true;}});check(wrong_thread.get());
  w.recording=true;w.action("show_histogram");w.action("configure_histogram",{{"custom",true},{"bins","4"},{"lower","140"},{"upper","164"},{"roi",false}});
  w.action("configure_histogram_target",{{"target",{{"scope","all_z"},{"roi","whole_plane"}}}});
  check(provider->worker_reads==0 && provider->main_reads==2);w.action("calculate_histogram");w.action("wait_histogram");
  auto report=imagejxx::desktop::HistogramAsyncUiProbeAccess::diagnostics(w);auto baseline=report.at("panel").at("result");
  check(baseline.at("counts")==Json::array({6,6,6,6}) && provider->worker_reads==3 && provider->main_reads==2 && w.workflow.size()==1);
  provider->fail=true;w.action("calculate_histogram");refuses([&]{w.action("wait_histogram");});
  check(imagejxx::desktop::HistogramAsyncUiProbeAccess::diagnostics(w).at("panel").at("result")==baseline && w.workflow.size()==1 && !w.busy());
  // A failed navigation read cannot replace the useful prior decoded frame.
  facade->setView({{"t",0},{"c",0},{"crosshair",{0,0,0}}});
  refuses([&]{facade->sharedFrame();});
  facade->setView({{"t",1},{"c",1},{"crosshair",{0,0,2}}});
  check(facade->sharedFrame()==next && provider->main_reads==3);
  provider->fail=false;check(owner->undo_steps()==1);w.closeDocument(owner->id,true);check(w.documents().empty());
  auto bounded=std::make_shared<Source>();auto boundedOwner=std::make_shared<Document>(Dataset(bounded));
  imagejxx::desktop::Document boundedFacade(boundedOwner,true,32);auto boundedCopy=boundedFacade.snapshot();
  refuses([&]{boundedCopy.frame();});check(bounded->main_reads==0);
  auto huge=std::make_shared<Source>();huge->sizes={1,1,1000000,100000,100000,1};auto big=std::make_shared<Document>(Dataset(huge));
  Window h((root/"huge.json").u8string());h.addVirtualDocument(big,1024);check(huge->main_reads==0 && h.document()->state().frames.empty() && h.documentInfo().at("axis_sizes").at("X")==100000);
  refuses([&]{h.document()->frame();});refuses([&]{h.document()->plane(Plane::XZ,0);});check(huge->main_reads==0 && huge->region_reads==0);
  auto replacement=std::make_shared<Source>();replacement->sizes={1,1,1,2,4,1};auto edit=big->begin_edit();edit.replace_dataset(Dataset(replacement));big->commit(edit);
  check(h.document()->sharedFrame()->at(3,1)==7 && replacement->main_reads==1);
  std::cout<<"PASS virtual desktop metadata attachment, lazy bounded viewing and worker histograms\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
