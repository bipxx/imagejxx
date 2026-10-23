#include <imagejxx/selection_region.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool v,const char *message){if(!v)throw std::runtime_error(message);}
template<class F>void refuses(F f){try{f();}catch(const std::exception &){return;}throw std::runtime_error("Expected refusal");}
int cells(const Selection &selection,int width,int height){int n=0;for(double v:selection.mask(width,height).pixels)n+=v!=0;return n;}
}
int main(){try{
 Image mask(9,9,1,PixelType::UInt8);mask.at(0,0)=255;
 const auto source=Selection::from_mask(mask);
 SelectionRegion region(source,9,9);
 const auto grown=region.enlarged(2);
 check(grown.bounds()==std::array<double,4>{-2,-2,3,3},"Off-image extent lost");
 check(cells(grown.local_selection(),grown.shape()[1],grown.shape()[0])==21,"Complete radius-2 membership differs");
 check(cells(grown.project(9,9),9,9)==8,"Explicit projection differs");
 const auto restored=grown.enlarged(-2);
 check(restored.project(9,9).mask(9,9).pixels==source.mask(9,9).pixels,"Repeated shrink used a clipped intermediate");
 check(cells(SelectionEditor(source,9,9).enlarged(2),9,9)==8,"Legacy canvas projection changed");
 SelectionRegion translated(source,9,9,{-100,200});
 const auto offset=translated.enlarged(2);
 check(offset.bounds()==std::array<double,4>{-102,198,-97,203},"Regional origin lost");
 check(cells(offset.project(9,9),9,9)==0,"Disjoint projection contains cells");
 SelectionEditLimits limit;limit.max_working_bytes=grown.estimate_projection(9,9)-1;
 refuses([&]{grown.project(9,9,limit);});
 auto token=std::make_shared<CancellationToken>();token->cancel();ExecutionControl control;control.cancellation=token;
 refuses([&]{region.enlarged(2,{},control);});
 ExecutionControl fail;fail.progress=[](std::size_t done,std::size_t total){if(done==total)throw std::runtime_error("finish");};
 refuses([&]{region.enlarged(2,{},fail);});refuses([&]{grown.project(9,9,{},fail);});
 check(region.project(9,9)==source,"Failure mutated region");
 // C++ value assignment can replace a source from inside its callback.
 // Results must use the complete pre-callback source, including its frame.
 SelectionRegion mutableRegion(source,9,9);ExecutionControl mutateRegion;
 mutateRegion.progress=[&](std::size_t done,std::size_t){if(done==0)mutableRegion=SelectionRegion(source,9,9,{1,1});};
 const auto frozenRegion=mutableRegion.enlarged(2,{},mutateRegion);
 check(frozenRegion.bounds()==std::array<double,4>{-2,-2,3,3},"Region enlargement did not freeze origin before callback");
 mutableRegion=SelectionRegion(source,9,9);
 check(mutableRegion.project(9,9,{},mutateRegion)==source,"Region projection did not freeze source before callback");
 SelectionEditor mutableEditor(source,9,9);ExecutionControl mutateEditor;
 mutateEditor.progress=[&](std::size_t done,std::size_t){if(done==0){Image other(5,5,1,PixelType::UInt8);other.at(1,1)=255;mutableEditor=SelectionEditor(Selection::from_mask(other),5,5);}};
 const auto frozenCanvas=mutableEditor.enlarged(2,{},mutateEditor);
 check(frozenCanvas.mask_width()==9 && frozenCanvas.mask_height()==9 && cells(frozenCanvas,9,9)==8,"Legacy enlargement did not freeze source/canvas before callback");
 mutableEditor=SelectionEditor(source,9,9);
 check(mutableEditor.enlarged_region(2,{},mutateEditor).bounds()==std::array<double,4>{-2,-2,3,3},"Full enlargement did not freeze source before callback");
 Selection far("polygon",{{1e10,1e10},{1e10+3,1e10},{1e10,1e10+3}});
 const auto farCanvas=SelectionEditor(far,9,9).enlarged(1);
 check(farCanvas.kind=="mask" && cells(farCanvas,9,9)==0,"Legacy far-off-canvas geometry was not preserved");
 refuses([&]{SelectionEditor(far,9,9).enlarged_region(1);});
 std::cout<<"PASS full selection region SDK off-grid/repetition/projection/limits/cancellation\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
