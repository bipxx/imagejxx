#include "window.hpp"
#include "virtual_view_cache.hpp"
#include <filesystem>
#include <future>
#include <iostream>
using namespace imagejxx;
using imagejxx::desktop::VirtualViewCache;
namespace {
void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){bool bad=false;try{f();}catch(const std::exception &){bad=true;}check(bad,"Expected rejection");}
struct Source:PlaneSource {
  int width=4,height=2;mutable int reads=0;bool fail=false,overreserve=false;
  DatasetShape shape()const override{return {1,1,2,height,width,1};}
  PixelType pixel_type()const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override {
    ++reads;if(fail)throw std::runtime_error("Read failed");
    auto image=std::make_shared<Image>(width,height,1,PixelType::UInt16);
    if(overreserve)image->pixels.reserve(128);
    for(std::size_t i=0;i<image->pixels.size();++i)image->pixels[i]=100*p.z+i;
    return image;
  }
};
}
int main(int argc,char **argv){try{
  VirtualViewCache cache(128);auto a=cache.slot(),b=cache.slot(),c=cache.slot();
  auto source=std::make_shared<Source>();Dataset data(source);
  auto first=cache.read(a,data,{0,0,0});cache.read(b,data,{0,0,1});
  check(cache.bytes()==128 && source->reads==2,"Initial retention");
  check(cache.read(a,data,{0,0,0})==first && source->reads==2,"Cache hit");
  cache.read(c,data,{0,0,0});
  check(!b->pixels && a->pixels==first && cache.bytes()==128 && cache.evictions()==1,"LRU eviction");
  source->fail=true;refuses([&]{cache.read(b,data,{0,0,0});});
  check(a->pixels && c->pixels && cache.bytes()==128,"Failed read preserved useful cache");
  source->fail=false;source->overreserve=true;
  refuses([&]{cache.read(b,data,{0,0,0});});
  check(cache.bytes()==128 && !b->pixels,"Actual capacity admission");source->overreserve=false;
  cache.setBudget(64);check(!a->pixels && c->pixels && cache.bytes()==64,"Immediate shrink eviction");
  check(first->at(3,1)==7,"Detached caller handle survived eviction");
  cache.setBudget(32);auto reads=source->reads;refuses([&]{cache.read(a,data,{0,0,0});});
  check(source->reads==reads && cache.bytes()==0,"Pre-read aggregate admission");
  refuses([&]{cache.setBudget(0);});check(cache.budget()==32,"Invalid budget preserved state");
  cache.setBudget(128);
  std::weak_ptr<Source> weak;
  {
    auto owned=std::make_shared<Source>();weak=owned;auto slot=cache.slot();
    cache.read(slot,Dataset(owned),{0,0,0});owned.reset();check(!weak.expired(),"Live slot owns source");
  }
  check(weak.expired() && cache.bytes()==0,"Closed slot releases provider and image");
  cache.read(a,data,{0,0,0});auto other=std::make_shared<Source>();Dataset replaced(other);
  cache.read(a,replaced,{0,0,0});check(other->reads==1 && a->source->shares_pixels(replaced),"Source identity invalidates cache");
  check(argc==2,"Owned fixture path required");namespace fs=std::filesystem;
  auto root=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(root),"Fresh fixture");fs::create_directories(root);
  using imagejxx::desktop::Window;using imagejxx::desktop::Json;
  auto rejectedSource=std::make_shared<Source>();
  auto rejectedOwner=std::make_shared<Document>(Dataset(rejectedSource));
  auto rejectedFacade=std::make_shared<imagejxx::desktop::Document>(rejectedOwner,true);
  auto preserved=rejectedFacade->sharedFrame();bool reject=false;
  auto constraint=rejectedOwner->constrain([&](const DocumentData &,const DisplayState &){if(reject)throw std::invalid_argument("Reject adoption");});
  Window failing((root/"failed-adoption.json").u8string());failing.updateSettings({{"default_zoom",3}},false);reject=true;
  refuses([&]{failing.addDocument(rejectedFacade);});reject=false;
  check(failing.documents().empty() && failing.virtualViewCacheInfo().at("entries")==0 && rejectedFacade->sharedFrame()==preserved && rejectedSource->reads==1,"Failed adoption preserves original frontend cache");
  Window w((root/"prefs.json").u8string());w.updateSettings({{"viewport_cache_mb",1}},true);
  auto big=std::make_shared<Source>();big->width=big->height=256;
  std::vector<std::shared_ptr<Document>> owners;
  for(int i=0;i<3;++i){auto owner=std::make_shared<Document>(Dataset(big));owners.push_back(owner);w.addVirtualDocument(owner);}
  auto da=w.documentById(owners[0]->id),db=w.documentById(owners[1]->id),dc=w.documentById(owners[2]->id);
  auto held=da->sharedFrame();db->sharedFrame();da->sharedFrame();dc->sharedFrame();
  check(big->reads==3 && w.virtualViewCacheInfo().at("entries")==2 && w.virtualViewCacheInfo().at("retained_bytes")==1048576,"Window shared budget");
  da->sharedFrame();check(big->reads==3,"Window LRU hit");db->sharedFrame();check(big->reads==4,"Window LRU miss");
  check(held->at(3,1)==259,"Window detached caller handle");
  auto worker=da->snapshot();worker.sharedFrame();check(big->reads==5 && w.virtualViewCacheInfo().at("retained_bytes")==1048576,"Worker cache is independent");
  w.updateSettings({{"viewport_cache_mb",2}},false);dc->sharedFrame();da->sharedFrame();db->sharedFrame();
  check(w.virtualViewCacheInfo().at("entries")==3,"Raised cache budget");
  w.updateSettings({{"viewport_cache_mb",1}},false);check(w.virtualViewCacheInfo().at("entries")==2,"Preferences shrink evicts immediately");
  auto before=w.virtualViewCacheInfo();refuses([&]{w.updateSettings({{"viewport_cache_mb",0}},false);});
  check(w.virtualViewCacheInfo()==before,"Invalid preference leaves cache unchanged");
  Window reopened((root/"prefs.json").u8string());check(reopened.virtualViewCacheInfo().at("budget_bytes")==1048576,"Persisted resource setting");
  auto foreign=std::async(std::launch::async,[&]{try{w.virtualViewCacheInfo();return false;}catch(const std::exception &){return true;}});
  check(foreign.get(),"Creator-thread diagnostics");
  std::cout<<"PASS aggregate virtual XY cache LRU, capacity limits, failures, lifetime, snapshots and preferences\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
