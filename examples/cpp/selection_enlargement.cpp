// Public core-only SDK consumer; no desktop or source sample reads.
#include <imagejxx/selection_edit.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
class Unread final : public PlaneSource {
public:
 DatasetShape shape() const override{return {2,2,2,9,9,1};}
 PixelType pixel_type() const override{return PixelType::UInt16;}
 std::shared_ptr<const Image> read_plane(PlanePosition) const override{throw std::runtime_error("Forbidden source read");}
};
}
int main(){try{
 Document owner{Dataset(std::make_shared<Unread>())};
 Image input(9,9,1,PixelType::UInt8);input.at(4,4)=255;
 owner.select(Selection::from_mask(input));const auto before=owner.snapshot();
 SelectionEditor editor(*before.data().selection,9,9);
 const auto estimate=editor.estimate_enlargement(2);check(estimate.rounded_pixels==2,"Rounded radius differs");
 const auto result=editor.enlarged(2);const auto mask=result.mask(9,9);
 int cells=0;for(int y=0;y<9;++y)for(int x=0;x<9;++x){
   const int dx=x-4,dy=y-4;const bool expected=dx*dx+dy*dy<=6;
   check((mask.at(x,y)!=0)==expected,"Rounded Euclidean dilation differs from literal cells");cells+=mask.at(x,y)!=0;
 }
 check(cells==21,"Radius2 must select21 cells, not a square25");
 owner.select(result);check(owner.snapshot().data().dataset.shares_pixels(before.data().dataset),"Geometry replaced pixels");
 check(owner.undo_steps()==0 && !owner.snapshot().dirty(),"Geometry changed persistent history");
 SelectionEditLimits cap;cap.max_working_bytes=estimate.working_bytes-1;int callbacks=0;ExecutionControl control;
 control.checkpoint=[&]{++callbacks;};bool refused=false;
 try{editor.enlarged(2,cap,control);}catch(const std::length_error &){refused=true;}
 check(refused && callbacks==0,"Resource refusal invoked callbacks");
 cap.max_working_bytes=estimate.working_bytes;check(editor.enlarged(2,cap).kind=="mask","Exact estimate refused");
 control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();refused=false;
 try{editor.enlarged(2,{},control);}catch(const OperationCancelled &){refused=true;}
 check(refused,"Cancellation ignored");
 bool in_edm=false;control.cancellation=std::make_shared<CancellationToken>();
 control.stage_progress=[&](std::string_view stage,std::size_t,std::size_t){
   if(stage=="Enlarge EDM forward"){in_edm=true;control.cancellation->cancel();}
 };
 refused=false;try{editor.enlarged(2,{},control);}catch(const OperationCancelled &){refused=true;}
 check(in_edm && refused,"Cancellation inside the EDM ignored");
 SelectionEditor box(Selection("rectangle",{{2,3},{7,8}}),9,9);
 check(box.enlarged(.5).points==std::vector<std::array<double,2>>{{1,2},{8,9}},"Half-up analytic growth differs");
 check(box.enlarged(-100)==box.selection(),"Collapsed shrink must retain original");
 std::cout<<"PASS enlargement SDK literal21/analytic/limits/cancel/no-IO\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
