// Read actual UI-written preferences using the installed C++17 public SDK.
#include <imagejxx/preferences.hpp>
#include <imagejxx/selection_enlargement.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace imagejxx;
namespace {
std::size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
std::string bytes(const char *path){
  std::ifstream stream(path,std::ios::binary);
  if(!stream)throw std::runtime_error("Cannot read UI preferences");
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
}
int main(int argc,char **argv){try{
  check(argc==2,"Use settings_selection ACTUAL_UI_PREFERENCES_PATH");
  const auto original=bytes(argv[1]);
  const auto preferences=PreferencesDocument::load(argv[1],false);
  check(!preferences.write_blocked && preferences.warnings.empty(),"Strict UI file load");
  const auto magnitude=preferences.settings.number("selection_enlargement_distance");
  const auto units=preferences.settings.text("selection_enlargement_units");
  check(magnitude==3. && units=="calibrated","Actual saved enlargement defaults");
  for(bool scientific:{false,true}){
    const DatasetShape shape=scientific?DatasetShape{2,2,2,17,17,1}:DatasetShape{1,1,1,17,17,1};
    Stack planes;
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
      Image image(17,17,1,PixelType::UInt8);
      for(int y=0;y<17;++y)for(int x=0;x<17;++x)image.at(x,y)=(19*t+31*c+47*z+3*y+x)%256;
      planes.push_back(std::move(image));
    }
    SpatialCalibration calibration;calibration.spacing={.5,1.5,2};calibration.unit="um";
    DatasetReadObserver reads;
    Document owner(Dataset::from_planes(shape,std::move(planes),calibration).observe_reads(reads));
    const Selection initial("rectangle",{{4,4},{12,11}});
    owner.select(initial);
    auto display=owner.snapshot().display();display.crosshair={8,8,scientific?1:0};
    display.t=display.c=scientific?1:0;owner.set_display(display);
    for(int sign:{1,-1}){
      owner.select(initial);const auto before=owner.snapshot();
      SelectionEnlargementRequest request;
      request.distance=sign*magnitude*calibration.spacing[0];
      request.units=SelectionEnlargementUnits::Calibrated;
      request.geometry=SelectionEnlargementGeometry::FullRegionV2;
      const auto command=resolve_selection_enlargement_command(before,request);
      check(command.estimate().pixel_distance==sign*3.,"Stored pixel magnitude consumed");
      const auto result=command.result().mask(17,17);
      const int left=sign>0?1:7,top=sign>0?1:7,right=sign>0?15:9,bottom=sign>0?14:8;
      for(int y=0;y<17;++y)for(int x=0;x<17;++x)
        check(result.at(x,y)==(x>=left && x<right && y>=top && y<bottom?255.:0.),"Literal UI-default rectangle mask");
      check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Enlargement acquired source samples");
      auto edit=prepare_selection_enlargement(command);owner.commit(edit);
      const auto after=owner.snapshot();
      check(bool(after.data().selection),"Published selection is absent");
      const auto published=after.data().selection->mask(17,17);
      for(int y=0;y<17;++y)for(int x=0;x<17;++x)
        check(published.at(x,y)==(x>=left && x<right && y>=top && y<bottom?255.:0.),"Published literal rectangle mask");
      check(after.data().previous_selection==initial,"Previous must be original selection");
      check(after.data().dataset.shares_pixels(before.data().dataset),"Source words changed");
      check(after.dirty()==before.dirty() && owner.undo_steps()==0 && owner.redo_steps()==0,"Metadata-only selection history");
      check(after.display().t==display.t && after.display().c==display.c && after.display().crosshair==display.crosshair,"Navigation changed");
      check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Publication acquired samples");
    }
  }
  check(bytes(argv[1])==original,"SDK consumer rewrote the UI file");
  std::cout<<checks<<" installed enlargement preferences checks passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
