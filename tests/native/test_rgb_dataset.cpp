#include <imagejxx/application.hpp>
#include <imagejxx/rgb_dataset_command.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace imagejxx;
void check(bool b){if(!b)throw std::runtime_error("RGB dataset assertion");}
template<class F>void refuses(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Expected RGB dataset refusal");}
int main(int argc,char **argv){try{
  check(argc==2);std::ifstream file(argv[1]);auto corpus=nlohmann::json::parse(file);check(corpus.at("cases").size()==111);
  for(const auto &row:corpus.at("cases")){
    const auto name=row.at("source_dtype").get<std::string>();auto type=name=="uint8"?PixelType::UInt8:name=="uint16"?PixelType::UInt16:PixelType::Float32;
    const int samples=row.at("source_samples");Image image(8,2,samples,type);image.pixels=row.at("source").get<std::vector<double>>();
    DatasetShape shape{2,1,2,2,8,samples};Document owner(Dataset::from_planes(shape,Stack(4,image)));RgbDatasetRequest r;r.destination=OutputDestination::InPlace;
    if(!row.at("display_range").is_null())r.display_range=row.at("display_range").get<std::array<double,2>>();
    if(!row.at("lut").is_null()){r.lut.emplace();auto kind=row.at("lut").get<std::string>();for(int i=0;i<256;++i){int v=kind=="inverse"?255-i:i;(*r.lut)[i]={std::uint8_t(kind=="custom"?(17*i+3)%256:v),std::uint8_t(kind=="custom"?255-i:v),std::uint8_t(kind=="custom"?(i*i+7)%256:v)};}}
    auto command=resolve_rgb_dataset_command(owner.snapshot(),r);validate_rgb_dataset_command_record(command.record());
    auto rebound=rgb_dataset_command_from_record(owner.snapshot(),command.record());auto plan=prepare_rgb_dataset(rebound);
    for(int t=0;t<2;++t)for(int z=0;z<2;++z)check(plan.preview().data().dataset.plane({t,0,z})->pixels==row.at("pixels").get<std::vector<double>>());
    auto tx=plan.begin_edit();owner.commit(tx);check(owner.snapshot().data().dataset.shape().s==3);
  }
  DatasetShape shape{2,2,2,2,3,1};Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){Image image(3,2,1,PixelType::UInt8);std::fill(image.pixels.begin(),image.pixels.end(),10+10*c+3*t+z);planes.push_back(std::move(image));}
  DatasetReadObserver reads;Document original(Dataset::from_planes(shape,std::move(planes)).observe_reads(reads));
  auto edit=original.begin_edit();edit.set_rois({Roi{"one",Selection("rectangle",{{0,0},{1,1}}),1,1,1,{}}});
  edit.set_extension(planar_annotations_extension,encode_planar_annotations(shape,{{"area",Selection("rectangle",{{0,0},{1,1}}),Plane::XZ,0,1,1,{}}}));
  edit.set_extension(planar_display_annotations_extension,encode_planar_display_annotations(shape,{{"text",TextAnnotationPayload{},Plane::YZ,1,0,1,{}}}));
  original.commit(edit);auto owner=std::make_shared<Document>(original.snapshot());auto view=owner->snapshot().display();view.c=1;
  ChannelDisplayState channels;channels.channels.resize(2);channels.channels[0].lut.name="Red";channels.channels[1].lut.name="Green";
  view.properties["channel_display"]=metadata_object(encode_channel_display(channels,shape,PixelType::UInt8,FloatingSamplePolicy::FiniteOnly));owner->set_display(view);
  RgbDatasetRequest request;request.mode=RgbDatasetMode::Composite;request.destination=OutputDestination::InPlace;
  auto command=resolve_rgb_dataset_command(owner->snapshot(),request);check(reads.counts().plane_reads==0);
  auto tiny=request;tiny.max_working_bytes=1;refuses([&]{resolve_rgb_dataset_command(owner->snapshot(),tiny);});check(reads.counts().plane_reads==0);
  auto plan=prepare_rgb_dataset(command);check(plan.preview().data().dataset.shape().c==1 && plan.preview().display().c==0 && plan.preview().display().properties.empty());
  for(int t=0;t<2;++t)for(int z=0;z<2;++z){auto rgb=plan.preview().data().dataset.plane({t,0,z});check(rgb->pixels[0]==10+3*t+z && rgb->pixels[1]==20+3*t+z && rgb->pixels[2]==0);}
  check(planar_annotations(plan.preview().data())[0].c==0 && planar_display_annotations(plan.preview().data())[0].c==0 && plan.preview().data().rois[0].c==0);
  auto tx=plan.begin_edit();check(tx.preview().display().properties.empty());owner->commit(tx);owner->undo();check(owner->snapshot().data().dataset.shape().c==2);owner->redo();check(owner->snapshot().data().dataset.shape().s==3);
  // A deferred single-channel palette must survive the renderer's usual
  // Composite-to-grayscale rule, both with and without an inherited range.
  for(bool explicit_range:{false,true}) {
    Image image(3,1,1,PixelType::UInt16);image.pixels={0,128,255};
    DatasetReadObserver palette_reads;Document single(Dataset::from_planes({2,1,2,1,3,1},Stack(4,image)).observe_reads(palette_reads));
    if(explicit_range){auto v=single.snapshot().display();v.properties["display_range"]=metadata_array({0.,255.});single.set_display(v);}
    RgbDatasetRequest palette_request;palette_request.mode=RgbDatasetMode::Composite;palette_request.lut=channel_display_lut("Red");
    auto palette_command=resolve_rgb_dataset_command(single.snapshot(),palette_request);check(palette_reads.counts().plane_reads==0);
    std::size_t checkpoints=0;ExecutionControl control;control.checkpoint=[&]{++checkpoints;};
    refuses([&]{prepare_rgb_dataset(palette_command,control,palette_command.layout().working_bytes-1);});
    check(checkpoints==0 && palette_reads.counts().plane_reads==0);
    validate_rgb_dataset_command_record(palette_command.record());
    auto restored=rgb_dataset_command_from_record(single.snapshot(),palette_command.record());
    auto palette_plan=prepare_rgb_dataset(restored);
    for(int t=0;t<2;++t)for(int z=0;z<2;++z)check(palette_plan.preview().data().dataset.plane({t,0,z})->pixels==std::vector<double>({0,0,0,128,0,0,255,0,0}));
  }
  std::cout<<"PASS RGB dataset:111 processor references, composite TCZ, deferred palettes, metadata, history and admission\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
