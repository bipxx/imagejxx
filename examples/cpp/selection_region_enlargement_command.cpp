#include <imagejxx/selection_enlargement.hpp>
#include <imagejxx/selection_region.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
class Unread final:public PlaneSource {
 public:
 DatasetShape shape()const override{return {2,2,2,9,9,1};}
 PixelType pixel_type()const override{return PixelType::UInt16;}
 std::shared_ptr<const Image> read_plane(PlanePosition)const override{throw std::runtime_error("Forbidden image read");}
};
int cells(const Selection &s,int w=9,int h=9){int n=0;for(double p:s.mask(w,h).pixels)n+=p!=0;return n;}
}
int main(){try{
 Document owner{Dataset(std::make_shared<Unread>())};
 Image pixels(9,9,1,PixelType::UInt8);pixels.at(0,0)=255;
 auto base=Selection::from_mask(pixels);base.attributes["origin"]=std::string("opaque");
 owner.select(base);const auto initial=owner.snapshot();
 SelectionEnlargementRequest request{2,SelectionEnlargementUnits::Pixels,SelectionEnlargementGeometry::FullRegionV2};
 auto grown=resolve_selection_enlargement_command(initial,request);
 check(std::get<std::int64_t>(grown.record().at("version"))==2,"V2 record missing");
 check(grown.result().is_region() && grown.result().mask_origin()==std::array<int,2>{-3,-3},"Full frame lost");
 check(cells(grown.result())==8,"Visible membership differs");
 check(cells(SelectionRegion(grown.result(),9,9).local_selection(),7,7)==21,"Full membership differs");
 validate_selection_enlargement_command_record(grown.record());
 const auto replay=selection_enlargement_command_from_record(initial,grown.record());
 check(replay.result()==grown.result(),"Recorded full geometry changed");
 auto edit=prepare_selection_enlargement(replay);owner.commit(edit);
 check(owner.undo_steps()==0 && !owner.snapshot().dirty(),"Selection changed pixel history");
 request.distance=-2;
 const auto shrunk=resolve_selection_enlargement_command(owner.snapshot(),request);
 check(cells(shrunk.result())==1,"Repeated command used clipped geometry");
 auto zero=request;zero.distance=0;
 check(!resolve_selection_enlargement_command(initial,zero).changes_selection(),"Zero command changed storage kind");
 auto erased=request;erased.distance=-100;
 check(!resolve_selection_enlargement_command(initial,erased).changes_selection(),"Erased shrink changed source storage kind");
 Document empty_owner{Dataset(std::make_shared<Unread>())};empty_owner.select(Selection::from_mask(Image(9,9,1,PixelType::UInt8)));
 auto empty_request=request;empty_request.distance=2;
 check(!resolve_selection_enlargement_command(empty_owner.snapshot(),empty_request).changes_selection(),"Empty mask changed storage kind");
 SelectionEnlargementCommandLimits cap;cap.max_working_bytes=grown.estimate().working_bytes-1;
 bool refused=false;try{auto grow=request;grow.distance=2;resolve_selection_enlargement_command(initial,grow,cap);}catch(const std::exception&){refused=true;}
 check(refused,"Below-estimate cap accepted");
 MetadataValue nested=std::int64_t(0);
 for(int level=0;level<127;++level)nested=metadata_array({nested});
 auto deep=base;deep.attributes["deep"]=nested;
 Document deep_owner{Dataset(std::make_shared<Unread>())};deep_owner.select(deep);
 int callbacks=0;ExecutionControl depth_control;depth_control.progress=[&](std::size_t,std::size_t){++callbacks;};
 refused=false;try{auto grow=request;grow.distance=2;resolve_selection_enlargement_command(deep_owner.snapshot(),grow,{},depth_control);}catch(const std::invalid_argument&){refused=true;}
 check(refused && callbacks==0,"Nested record admission reached callbacks");
 // A caller may replace the public command value inside its callback. The
 // admitted command/source must remain the one being prepared.
 Document other{Dataset(std::make_shared<Unread>())};other.select(base);
 auto replacement_request=request;replacement_request.distance=4;
 const auto replacement=resolve_selection_enlargement_command(other.snapshot(),replacement_request);
 auto mutable_command=shrunk;ExecutionControl control;
 control.progress=[&](std::size_t done,std::size_t){if(!done)mutable_command=replacement;};
 auto frozen_edit=prepare_selection_enlargement(mutable_command,control);owner.commit(frozen_edit);
 check(cells(*owner.snapshot().data().selection)==1,"Preparation adopted callback replacement");
 check(owner.snapshot().data().dataset.shares_pixels(initial.data().dataset),"Geometry read/replaced pixels");
 std::cout<<"PASS V2 full-region enlargement capture/replay/prepare/frozen source\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
