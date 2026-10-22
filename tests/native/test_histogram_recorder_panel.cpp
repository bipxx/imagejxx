// Direct native panel callback checks. The desktop Window adapter currently
// refuses virtual datasets; these fixtures do not claim that adapter support.
#include "../../src/desktop/histogram_panel.hpp"
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
using imagejxx::desktop::HistogramPanel;
using imagejxx::desktop::Json;
struct Source final:PlaneSource {
  mutable std::function<void()> callback;
  DatasetShape shape() const override{return {1,1,1,2,4,1};}
  PixelType pixel_type() const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    auto work=std::move(callback);callback={};if(work)work();
    Image plane(4,2,1,PixelType::UInt16);for(int i=0;i<8;++i)plane.pixels[i]=i;
    return std::make_shared<const Image>(std::move(plane));
  }
};
void require(bool ok){if(!ok)throw std::runtime_error("Histogram panel stale publication guard failed");}
int main(){try {
  for(int kind=0;kind<4;++kind) {
    auto source=std::make_shared<Source>();Document owner{Dataset(source)};HistogramPanel panel;
    panel.show(owner.snapshot(),{},Settings{});int published=0;
    auto publish=[&](const SettingsPatch &,const MetadataObject &record){
      // Same live-source check used by the Window publication boundary.
      (void)histogram_command_from_record(owner.snapshot(),record);++published;
    };
    panel.calculate(owner.snapshot(),false,publish);
    const auto result=panel.diagnostics().at("result");require(published==1);
    source->callback=[&]{
      if(kind==0)panel.configure({{"custom",true},{"bins","2"},{"lower","0"},{"upper","8"}});
      if(kind==1){auto edit=owner.begin_edit();edit.set_title("provider metadata change");owner.commit(edit);}
      if(kind==2)panel.sourceClosed(owner.id);
      if(kind==3)throw std::runtime_error("provider failure");
    };
    bool refused=false;try{panel.calculate(owner.snapshot(),false,publish);}catch(const std::exception &){refused=true;}
    const auto after=panel.diagnostics();require(refused && published==1 && after.at("result")==result);
    if(kind==0)require(after.at("custom")==true && after.at("bins")=="2");
    if(kind==2)require(after.at("has_source")==false);
    require(owner.undo_steps()==(kind==1?1:0));
  }
  std::cout<<"histogram panel draft/source/close/provider callback publication guards passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
