#include <imagejxx/color_stack_command.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
template<class E=std::invalid_argument,class F>void rejects(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught,"Expected refusal");}
class Source final:public PlaneSource {
public:
 DatasetShape shape()const override{return {2,2,2,1,2,3};}
 PixelType pixel_type()const override{return PixelType::UInt8;}
 std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
  ++reads;auto a=std::make_shared<Image>(2,1,3,PixelType::UInt8);a->pixels={double(100*p.t+20*p.c+3*p.z),10,20,0,255,0};return a;
 }
 mutable int reads=0;
};
class ScalarSource final:public PlaneSource {
public:
 DatasetShape shape()const override{return {1,2,2,1,2,1};}
 PixelType pixel_type()const override{return PixelType::UInt16;}
 std::shared_ptr<const Image> read_plane(PlanePosition p)const override{++reads;auto image=std::make_shared<Image>(2,1,1,PixelType::UInt16);const double low=1000*(2*p.z+p.c);image->pixels={low,low+1000};return image;}
 mutable int reads=0;
};
void scalar_flatten(){
 auto provider=std::make_shared<ScalarSource>();SpatialCalibration cal;cal.unit="um";cal.spacing={2,3,5};auto owner=std::make_shared<Document>(Dataset(provider,cal));auto view=owner->snapshot().display();view.c=1;view.crosshair[2]=1;owner->set_display(view);const auto source=owner->snapshot();
 ColorStackRequest request;request.action=ColorStackAction::FlattenRgb;request.layout=ColorStackLayoutPolicy::ImageJLegacy;request.allow_calibration_reset=true;request.destination=OutputDestination::InPlace;
 auto command=resolve_color_stack_command(source,request);validate_color_stack_command_record(command.record());check(provider->reads==0,"Scalar resolve read samples");check(metadata_equal(color_stack_command_from_record(source,command.record()).record(),command.record()),"Scalar V2 replay");
 auto prepared=prepare_color_stack(command);check(provider->reads==4,"Opening scalar plane should be reused");check(prepared.preview().data().dataset.shape().z==4 && prepared.preview().data().dataset.shape().s==3,"Scalar flatten shape");
 check(prepared.preview().data().dataset.plane({0,0,0})->pixels==std::vector<double>(6,0),"Scalar default range must be common opening range");
 auto image=prepared.preview().data().dataset.plane({0,0,3});check(image->pixels.front()==0 && image->pixels.back()==255,"Opening range quantization");
 Application app;app.adopt(owner);app.publish(prepared);check(owner->history().undo_steps==1,"Scalar publish is one edit");owner->undo();check(owner->snapshot().data().dataset.pixel_type()==PixelType::UInt16 && owner->snapshot().data().dataset.calibration().unit=="um","Scalar paired Undo");owner->redo();check(owner->snapshot().data().dataset.shape().z==4,"Scalar redo");
 RgbDatasetRequest rendering;rendering.display_range=std::array<double,2>{0,4000};rendering.lut=channel_display_lut("Red");auto explicit_command=resolve_color_stack_command(source,request,rendering);auto explicit_result=prepare_color_stack(explicit_command);const auto last=explicit_result.preview().data().dataset.plane({0,0,3});check(last->pixels[1]==0 && last->pixels[2]==0 && last->pixels[3]==255,"Explicit scalar RGB renderer");
 class FloatSource final:public PlaneSource {
 public:
  DatasetShape shape()const override{return {1,2,1,1,2,1};}
  PixelType pixel_type()const override{return PixelType::Float32;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{auto image=std::make_shared<Image>(2,1,1,PixelType::Float32);image->pixels=p.c?std::vector<double>{1,2}:std::vector<double>{1e-50,1e-50};return image;}
 };
 Document tiny{Dataset{std::make_shared<FloatSource>()}};auto tiny_result=prepare_color_stack(resolve_color_stack_command(tiny.snapshot(),request));
 check(tiny_result.preview().data().dataset.plane({0,0,1})->pixels==std::vector<double>{0,0,0,255,255,255},"Deferred reset must observe declared Float32 samples before testing the zero sentinel");
}
void run(){
 auto provider=std::make_shared<Source>();SpatialCalibration cal;cal.spacing={2,3,5};cal.origin={10,20,30};cal.unit="um";cal.frame_interval=2;
 auto owner=std::make_shared<Document>(Dataset(provider,cal));auto tx=owner->begin_edit();Selection selection("rectangle",{{0,0},{1,1}});tx.select(selection);tx.set_rois({Roi{"position",selection,1,1,1,{}},Roi{"wild",selection,{},{},{},{}}});owner->commit(tx);
 auto before=owner->snapshot();ColorStackRequest request;request.destination=OutputDestination::InPlace;
 auto command=resolve_color_stack_command(before,request);check(provider->reads==0,"Resolution read pixels");check(command.layout().shape.c==6,"Split channels");
 validate_color_stack_command_record(command.record());check(metadata_equal(color_stack_command_from_record(before,command.record()).record(),command.record()),"Record replay");
 auto prepared=prepare_color_stack(command);check(provider->reads==8,"Split must read each source once");check(prepared.preview().data().rois.size()==4,"ROI fanout");
 check(prepared.preview().data().dataset.calibration().spacing==cal.spacing,"Lost calibration");check(owner->history().undo_steps==1,"Preparation changed history");
 Application app;app.adopt(owner);app.publish(prepared);check(owner->snapshot().data().dataset.shape().c==6,"Publish split");owner->undo();check(owner->snapshot().data().dataset.shape().s==3,"Undo split");owner->redo();check(owner->snapshot().data().dataset.shape().s==1,"Redo split");
 ColorStackRequest merge;merge.action=ColorStackAction::Merge;auto combined=prepare_color_stack(resolve_color_stack_command(owner->snapshot(),merge));
 check(combined.preview().data().dataset.shape().dimensions()==before.data().dataset.shape().dimensions(),"Merge shape");
 check(combined.preview().data().dataset.plane({1,1,1})->pixels==before.data().dataset.plane({1,1,1})->pixels,"RGB round trip");
 request.layout=ColorStackLayoutPolicy::ImageJLegacy;request.space=ColorComponentSpace::HSB32;
 rejects([&](){resolve_color_stack_command(before,request);});request.allow_calibration_reset=true;
 auto legacy=resolve_color_stack_command(before,request);check(legacy.layout().shape.z==8 && legacy.layout().shape.t==1 && legacy.layout().calibration_reset,"Legacy shape");
 auto split=prepare_color_stack(legacy);check(split.preview().data().dataset.calibration().unit=="px","Legacy calibration");
 ColorStackRequest flat;flat.action=ColorStackAction::FlattenRgb;flat.layout=ColorStackLayoutPolicy::ImageJLegacy;flat.allow_calibration_reset=true;
 int reads=provider->reads;auto flattened=prepare_color_stack(resolve_color_stack_command(before,flat));check(provider->reads==reads,"Flatten eagerly read samples");
 for(int t=0;t<2;++t)for(int z=0;z<2;++z)for(int c=0;c<2;++c)check(flattened.preview().data().dataset.plane({0,0,(t*2+z)*2+c})->pixels==before.data().dataset.plane({t,c,z})->pixels,"C-fast flatten order");
 ExecutionControl control;int callbacks=0;control.checkpoint=[&](){++callbacks;};reads=provider->reads;
 rejects([&](){prepare_color_stack(command,control,command.layout().working_bytes-1);});check(callbacks==0 && provider->reads==reads,"Cap refusal too late");
 control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();rejects<OperationCancelled>([&](){prepare_color_stack(command,control);});check(callbacks==0 && provider->reads==reads,"Pre-cancel too late");
 control={};control.cancellation=std::make_shared<CancellationToken>();control.progress=[&](std::size_t,std::size_t){control.cancellation->cancel();};rejects<OperationCancelled>([&](){prepare_color_stack(command,control);});
 auto malformed=command.record();malformed["extra"]=1;rejects([&](){validate_color_stack_command_record(malformed);});
 auto other=Document(before);rejects<RevisionConflict>([&](){color_stack_command_from_record(other.snapshot(),command.record());});
 Document single(Dataset::from_planes({1,1,3,1,2,3},Stack(3,Image(2,1,3,PixelType::UInt8)),cal));
 auto edit=single.begin_edit();edit.set_rois({Roi{"all Z",selection,0,0,{},{}}});single.commit(edit);
 auto annotation=prepare_store_planar_annotation(single.snapshot(),PlanarAnnotation{"XZ",selection,Plane::XZ,0,0,0,{}});single.commit(annotation);
 ColorStackRequest hsb;hsb.space=ColorComponentSpace::HSB32;hsb.layout=ColorStackLayoutPolicy::ImageJLegacy;
 auto once=prepare_color_stack(resolve_color_stack_command(single.snapshot(),hsb));
 check(once.preview().data().rois.size()==1 && planar_annotations(once.preview().data()).size()==1,"Legacy spanning annotation duplicated");
}
}
int main(){try{run();scalar_flatten();std::cout<<"Color stack dataset contracts passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
