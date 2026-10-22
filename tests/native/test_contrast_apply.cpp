#include <imagejxx/contrast_apply.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/channel_display.hpp>
#include <algorithm>
#include <atomic>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool v,const char *why){if(!v)throw std::runtime_error(why);}
template<class E=std::exception,class F>void refuses(F f){try{f();}catch(const E &){return;}throw std::runtime_error("Expected refusal");}
Image row(PixelType type,std::vector<double> p){Image image(int(p.size()),1,1,type);image.pixels=std::move(p);return image;}
void literals(){
  auto image=row(PixelType::UInt8,{0,10,11,20,30,40,41,255});
  ContrastApplyOptions o;o.minimum=10.9;o.maximum=40.9;
  check(apply_contrast_imagej(image,o).pixels==std::vector<double>({0,0,8,85,170,255,255,255}),"Byte endpoints truncate, interior uses 256, max clips");
  o.minimum=o.maximum=10.9;
  check(apply_contrast_imagej(image,o).pixels==std::vector<double>({0,0,255,255,255,255,255,255}),"Collapsed integer endpoints are a step, without division");
  auto wide=row(PixelType::UInt16,{0,10,11,20,30,40,65535});o.minimum=10;o.maximum=40;
  check(apply_contrast_imagej(wide,o).pixels==std::vector<double>({0,0,2184,21845,43690,65535,65535}),"Automatic UInt16 maps full processor range");
  o.unsigned16_bits=12;
  check(apply_contrast_imagej(wide,o).pixels==std::vector<double>({0,0,136,1365,2730,4094,4094}),"Configured UInt16 retains ImageJ range-minus-one behavior");
  o={-2147483648.,2147483647.,0};
  check(apply_contrast_imagej(row(PixelType::UInt8,{0,1,255}),o).pixels==std::vector<double>({255,255,255}),"Extreme Java subtraction and byte narrowing");
  o={-2147483648.,1.,0};
  check(apply_contrast_imagej(row(PixelType::UInt8,{0,1,255}),o).pixels==std::vector<double>({0,255,255}),"Wrapped denominator is defined without C++ signed overflow");
  refuses([&]{apply_contrast_imagej(row(PixelType::Float32,{0,1}));});
  auto bad=image;bad.pixels[0]=.5;refuses([&]{apply_contrast_imagej(bad);});
  o={40,10,0};refuses([&]{apply_contrast_imagej(image,o);});
  o={0,255,9};refuses([&]{apply_contrast_imagej(image,o);});
  auto token=std::make_shared<CancellationToken>();token->cancel();ExecutionControl c;c.cancellation=token;
  refuses<OperationCancelled>([&]{apply_contrast_imagej(image,{},c);});
  for(const auto *phase:{"Contrast Apply validation","Contrast Apply pixels"}) {
    Image large(4096,1,1,PixelType::UInt8);std::fill(large.pixels.begin(),large.pixels.end(),20);
    token=std::make_shared<CancellationToken>();c.cancellation=token;
    c.stage_progress=[&](auto name,auto completed,auto){if(name==phase && completed>=1024)token->cancel();};
    refuses<OperationCancelled>([&]{apply_contrast_imagej(large,{10,40,0},c);});
    check(std::all_of(large.pixels.begin(),large.pixels.end(),[](double v){return v==20;}),"In-kernel cancel preserves input");
  }
}
struct Source:PlaneSource {
  mutable std::atomic<int> reads{0};
  DatasetShape shape()const override{return {1,2,2,1,4,1};}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    ++reads;auto image=row(PixelType::UInt8,{10,20,30,40});
    if(p.c==1)for(auto &v:image.pixels)v+=1;
    return std::make_shared<const Image>(std::move(image));
  }
};
void transaction(){
  auto source=std::make_shared<Source>();auto owner=std::make_shared<Document>(Dataset(source));
  owner->select(Selection::from_packed_mask(4,1,{0x60}));
  auto view=owner->snapshot().display();ChannelDisplayState state;state.channels.resize(2);
  state.channels[0].range={10,40};state.channels[1].range={11,41};
  view=with_channel_display(view,owner->snapshot().data().dataset,state);view.properties["retained"]=std::string("exact");owner->set_display(view);
  const auto before=owner->snapshot();const auto steps=owner->history().undo_steps;TargetRequest request;request.scope=TargetScope::AllZ;
  const auto target=resolve_target(before,request);ContrastApplyOptions options{10,40,0};
  ContrastApplyLimits tiny;tiny.max_working_bytes=1;
  refuses([&]{prepare_contrast_apply(target,options,tiny);});check(source->reads==0,"Admission precedes provider IO");
  auto prepared=prepare_contrast_apply(target,options);check(source->reads==2,"Only selected T/C/Z planes read");
  check(before.shares_data(owner->snapshot()) && owner->snapshot().revisions().display==before.revisions().display,"Prepare is isolated");
  owner->commit(prepared);auto applied=owner->snapshot();
  check(applied.data().dataset.plane({0,0,0})->pixels==std::vector<double>({10,85,170,40}),"Mask preserves exterior pixels");
  check(applied.data().dataset.plane({0,0,1})->pixels==std::vector<double>({10,85,170,40}),"All-Z targeting preserves source selection");
  auto changed=recognized_channel_display(applied.display(),applied.data().dataset);
  check(changed->channels[0].range==std::array<double,2>{0,255} && changed->channels[1].range==std::array<double,2>{11,41},"Reset only affected scientific channel");
  check(applied.display().properties.at("retained")==before.display().properties.at("retained"),"Unrelated display metadata retained");
  check(applied.dirty() && owner->history().undo_steps==steps+1,"Pixel Apply adds one history entry after selection");
  owner->undo();check(owner->snapshot().data().dataset.shares_pixels(before.data().dataset),"Undo restores pixel storage");
  check(recognized_channel_display(owner->snapshot().display(),before.data().dataset)->channels[0].range==std::array<double,2>{10,40},"Undo restores paired range");
  owner->redo();check(recognized_channel_display(owner->snapshot().display(),before.data().dataset)->channels[0].range==std::array<double,2>{0,255},"Redo restores range with pixels");
  auto stale=prepare_contrast_apply(resolve_target(owner->snapshot()),options);
  view=owner->snapshot().display();view.zoom=2;owner->set_display(view);
  const auto stable=owner->snapshot();refuses<RevisionConflict>([&]{owner->commit(stale);});check(stable.shares_data(owner->snapshot()),"Stale display rejects atomically");
  owner->select(Selection::from_packed_mask(4,1,{0}));const auto empty=owner->snapshot();const auto reads=source->reads.load();
  auto noop=prepare_contrast_apply(resolve_target(empty),options);owner->commit(noop);
  check(source->reads==reads && empty.shares_data(owner->snapshot()),"Empty selection performs no IO or edit");
}
void cancel(){
  auto owner=std::make_shared<Document>(Dataset::from_stack({row(PixelType::UInt8,{10,20,30,40})}));
  const auto before=owner->snapshot();ExecutionControl control;
  auto token=std::make_shared<CancellationToken>();control.cancellation=token;
  control.progress=[&](auto,auto){token->cancel();};
  refuses<OperationCancelled>([&]{prepare_contrast_apply(resolve_target(before),{10,40,0},{},control);});
  check(before.shares_data(owner->snapshot()) && !owner->snapshot().dirty(),"Cancel after last plane preserves owner");
}
}
int main(){try{literals();transaction();cancel();std::cout<<"PASS contrast Apply mapping, masks, targeting, atomic history, admission and cancellation\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
