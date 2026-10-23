#include <imagejxx/selection_region.hpp>
#include <imagejxx/session.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int cells(const Selection &s,int w,int h){int result=0;for(double v:s.mask(w,h).pixels)result+=v!=0;return result;}
}
int main(){try{
 Image pixels(9,9,1,PixelType::UInt16);pixels.pixels.assign(81,30);
 DatasetReadObserver reads;Document owner(Dataset::from_stack({pixels,pixels}).observe_reads(reads));
 Image mask(9,9,1,PixelType::UInt8);mask.at(0,0)=255;
 const auto area=SelectionRegion(Selection::from_mask(mask),9,9).enlarged(2).as_selection();
 check(area.is_region() && area.mask_origin()==std::array<int,2>{-3,-3},"Canonical frame missing");
 const auto before=owner.snapshot();owner.select(area);
 check(owner.undo_steps()==0 && !owner.snapshot().dirty() && owner.snapshot().data().dataset.shares_pixels(before.data().dataset),"Selection storage changed pixels/history");
 check(cells(*owner.snapshot().data().selection,9,9)==8,"Visible projection differs");
 check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Geometry storage read pixels");
 auto edit=owner.begin_edit();edit.set_rois({Roi{"Full region",area,{},{},1,{}}});owner.commit(edit);
 owner.select(Selection("rectangle",{{4,4},{6,6}}));
 check(owner.snapshot().data().previous_selection==area,"Previous lost full region");
 auto invalid=area;invalid.width=0;bool refused=false;
 try {auto transaction=owner.begin_edit();transaction.set_previous_selection(invalid);}
 catch(const std::invalid_argument &){refused=true;}
 check(refused && owner.snapshot().data().previous_selection==area,"Invalid regional Previous was published");
 SessionCapture capture{{owner.snapshot()},{},{},{}};const auto estimate=estimate_session(capture);
 const auto bytes=encode_session(capture);check(bytes.size()<=estimate.encoded_bytes,"Session bound failed");
 check(std::string(bytes.begin(),bytes.begin()+10)=="IMAGEJXX4\n","Regional session did not use V4");
 const auto loaded=decode_session(bytes);check(loaded.source_version==4 && loaded.documents.size()==1,"V4 decode failed");
 const auto restored=loaded.documents[0]->snapshot();
 check(restored.data().previous_selection==area && restored.data().rois[0].selection==area,"Regional annotations changed in session");
 const auto shrunk=SelectionEditor(*restored.data().previous_selection,9,9).enlarged(-2);
 check(cells(shrunk,9,9)==1,"Reopened edit used clipped geometry");
 const auto moved=SelectionEditor(area,9,9).moved({2,3});
 check(moved.is_region() && moved.mask_origin()==std::array<int,2>{-1,0},"Translation lost region frame");
 Document ordinary(Dataset::from_stack({pixels}));ordinary.select(Selection::from_mask(mask));
 const auto old=encode_session(SessionCapture{{ordinary.snapshot()},{},{},{}});
 check(std::string(old.begin(),old.begin()+10)=="IMAGEJXX3\n","Legacy-only session version changed");
 check(decode_session(old).source_version==3,"Legacy decode changed");
 std::cout<<"PASS canonical region storage/Previous/saved ROI/V4/legacy V3/reopened geometry\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
