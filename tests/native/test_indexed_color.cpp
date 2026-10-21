#include <imagejxx/indexed_color.hpp>
#include <imagejxx/indexed_dataset_command.hpp>
#include <imagejxx/rgb_dataset_command.hpp>
#include <imagejxx/application.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class E=std::invalid_argument,class F>void rejects(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught,"Expected indexed refusal");}
void references(const char *path){
 std::ifstream file(path);Json reference;file>>reference;check(reference.at("cases").size()==100,"Indexed fixture count");
 for(const auto &row:reference.at("cases")){
  const int h=row["shape"][0],w=row["shape"][1];Stack input;for(const auto &values:row["input"]){Image image(w,h,3,PixelType::UInt8);image.pixels=values.get<std::vector<double>>();input.push_back(std::move(image));}
  const auto result=quantize_indexed_color(input,row["max_colors"].get<int>());check(result.color_count==int(row["palette"].size()),"Indexed palette count");
  for(int i=0;i<result.color_count;++i)for(int c=0;c<3;++c)check(result.palette[i][c]==row["palette"][i][c].get<int>(),"Indexed palette mismatch");
  for(std::size_t i=0;i<result.indices.size();++i)check(result.indices[i].pixels==row["indices"][i].get<std::vector<double>>(),"Indexed indices mismatch");
  for(int i=result.color_count;i<256;++i)check(result.palette[i]==std::array<std::uint8_t,3>{0,0,0},"Indexed unused palette row");
 }
}
void contracts(){
 Image red(2,1,3,PixelType::UInt8);red.pixels={255,0,0,255,0,0};Stack input{red};ExecutionControl control;int callbacks=0;const auto bytes=indexed_color_working_bytes(2,1,1);
 control.checkpoint=[&]{++callbacks;};rejects([&]{quantize_indexed_color(input,256,control,bytes-1);});check(!callbacks,"Admission called callbacks");
 control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();rejects<OperationCancelled>([&]{quantize_indexed_color(input,256,control);});check(!callbacks,"Pre-cancel called callbacks");
 control={};control.checkpoint=[&]{input[0].pixels.assign(6,0);};const auto copied=quantize_indexed_color(input,256,control);check(copied.color_count==1&&copied.palette[0]==std::array<std::uint8_t,3>{255,0,0},"Callbacks changed owned indexed input");
 input={red};control={};control.cancellation=std::make_shared<CancellationToken>();control.progress=[&](std::size_t,std::size_t){control.cancellation->cancel();};rejects<OperationCancelled>([&]{quantize_indexed_color(input,256,control);});
 for(const int maximum:{-1,0,1,257})rejects([&]{quantize_indexed_color(input,maximum);});
 auto invalid=input;invalid[0].pixels[0]=.5;rejects([&]{quantize_indexed_color(invalid);});invalid[0].pixels[0]=std::numeric_limits<double>::quiet_NaN();rejects([&]{quantize_indexed_color(invalid);});
 rejects([&]{indexed_color_working_bytes(std::numeric_limits<int>::max(),2,1);});rejects([&]{indexed_color_working_bytes(2,2,std::numeric_limits<std::size_t>::max());});
}

void dataset_contracts(){
 class Source final:public PlaneSource {
 public:
  mutable int reads=0;DatasetShape shape()const override{return {2,2,2,1,2,3};}PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{++reads;auto image=std::make_shared<Image>(2,1,3,PixelType::UInt8);image->pixels={255,0,0,0,255,0};return image;}
 };
 auto provider=std::make_shared<Source>();SpatialCalibration cal;cal.spacing={2,3,5};cal.unit="um";auto owner=std::make_shared<Document>(Dataset(provider,cal));auto display=owner->snapshot().display();display.t=display.c=1;display.crosshair[2]=1;display.zoom=2;owner->set_display(display);const auto source=owner->snapshot();
 IndexedDatasetRequest request;request.max_colors=16;request.destination=OutputDestination::InPlace;auto command=resolve_indexed_dataset_command(source,request);validate_indexed_dataset_command_record(command.record());check(provider->reads==0,"Indexed resolution acquired samples");check(metadata_equal(indexed_dataset_command_from_record(source,command.record()).record(),command.record()),"Indexed record replay");
 auto prepared=prepare_indexed_dataset(command);check(provider->reads==8,"Indexed source read count");check(prepared.begin_edit().dependencies()==Change::All&&prepared.preview().data().dataset.shape().s==1,"Indexed dataset scope/type");
 const auto palette=indexed_palette(prepared.preview().data());check(palette&&palette->color_count==2,"Indexed stored palette");check(prepared.preview().display().c==1&&prepared.preview().display().t==1&&prepared.preview().display().zoom==2,"Indexed paired view");
 auto rgb=prepare_rgb_dataset(resolve_rgb_dataset_command(prepared.preview()));check(rgb.preview().data().dataset.plane({1,1,1})->pixels==std::vector<double>{255,0,0,0,255,0},"Palette-aware native RGB reconstruction");
 Application app;app.adopt(owner);app.publish(prepared);check(owner->undo_steps()==1,"Indexed one publication");owner->undo();check(owner->snapshot().data().dataset.shape().s==3&&!indexed_palette(owner->snapshot().data()),"Indexed Undo");owner->redo();check(indexed_palette(owner->snapshot().data())->color_count==2,"Indexed Redo");
 auto invalid=encode_indexed_palette(*palette);invalid["color_count"]=true;rejects([&]{indexed_palette_from_record(invalid);});rejects([&]{auto edit=owner->begin_edit();edit.set_extension(indexed_palette_extension,metadata_object(invalid));owner->commit(edit);});
 const int reads=provider->reads;rejects([&]{prepare_indexed_dataset(command,{},command.layout().working_bytes-1);});check(provider->reads==reads,"Indexed cap read pixels");
}

void type_references(const char *path){
 std::ifstream file(path);Json reference;file>>reference;check(reference.at("cases").size()==360,"Indexed type fixture count");
 for(const auto &row:reference.at("cases")){
  IndexedPalette palette;palette.color_count=int(row["source_palette"].size());for(int i=0;i<palette.color_count;++i)for(int c=0;c<3;++c)palette.palette[i][c]=row["source_palette"][i][c].get<unsigned char>();
  const auto kind=classify_indexed_palette(palette);check(kind.color==row["before"]["color"]&&kind.pseudo==row["before"]["pseudo"]&&kind.inverted==row["before"]["inverted"],"Indexed LUT classification");
  Stack planes;for(const auto &values:row["input"]){Image image(8,4,1,PixelType::UInt8);image.pixels=values.get<std::vector<double>>();planes.push_back(image);}
  DatasetReadObserver reads;Document owner(Dataset::from_planes({1,1,int(planes.size()),4,8,1},planes).observe_reads(reads));auto edit=owner.begin_edit();const auto encoded=encode_indexed_palette(palette);edit.set_extension(indexed_palette_extension,metadata_object(encoded));owner.commit(edit);
  auto display=owner.snapshot().display();display.crosshair[2]=int(planes.size())-1;display.properties["custom_lut"]=encoded.at("palette");if(row["range"]==1)display.properties["display_range"]=metadata_array({1.,127.});owner.set_display(display);
  TargetRequest request;request.scope=TargetScope::Ranges;request.roi=RoiPolicy::WholePlane;request.t=request.c=AxisRange{0,1,1};request.z=AxisRange{0,int(planes.size()),1};const auto target=resolve_target(owner.snapshot(),request);
  std::string operation=row["operation"],name;OperationParameters parameters;
  if(operation=="gaussian"){name="gaussian_blur_imagej";parameters={{"sigma",NumericArray{{2},{1.,1.}}}};}
  else if(operation=="median"){name="rank_filter_imagej";parameters={{"method",std::string("median")},{"radius",1.}};}
  else{name="convert_image";parameters={{"dtype",std::string(operation=="gray8"?"uint8":operation=="gray16"?"uint16":"float32")},{"weighted_rgb",row["weighted"].get<bool>()}};}
  if(!row["error"].is_null()){rejects([&]{prepare_operation(name,target,parameters);});check(reads.counts().plane_reads==0,"Indexed invalid type sampled input");continue;}
  auto prepared=prepare_operation(name,target,parameters);for(int z=0;z<int(planes.size());++z)check(prepared.preview().data().dataset.plane({0,0,z})->pixels==row["output"][z].get<std::vector<double>>(),"Indexed type/filter pixel mismatch");
 }
}
void filter_references(const char *path){
 std::ifstream file(path);Json reference;file>>reference;check(reference.at("cases").size()==288,"Indexed type fixture count");
 for(const auto &row:reference.at("cases")){
  IndexedPalette palette;palette.color_count=int(row["source_palette"].size());for(int i=0;i<palette.color_count;++i)for(int c=0;c<3;++c)palette.palette[i][c]=row["source_palette"][i][c].get<unsigned char>();
  const auto kind=classify_indexed_palette(palette);check(kind.color==row["before"]["color"]&&kind.pseudo==row["before"]["pseudo"]&&kind.inverted==row["before"]["inverted"],"Indexed LUT classification");
  Stack planes;for(const auto &values:row["input"]){Image image(8,4,1,PixelType::UInt8);image.pixels=values.get<std::vector<double>>();planes.push_back(image);}
  DatasetReadObserver reads;Document owner(Dataset::from_planes({1,1,int(planes.size()),4,8,1},planes).observe_reads(reads));auto edit=owner.begin_edit();const auto encoded=encode_indexed_palette(palette);edit.set_extension(indexed_palette_extension,metadata_object(encoded));owner.commit(edit);
  auto display=owner.snapshot().display();display.crosshair[2]=int(planes.size())-1;display.properties["custom_lut"]=encoded.at("palette");if(row["range"]==1)display.properties["display_range"]=metadata_array({1.,127.});owner.set_display(display);
  TargetRequest request;request.scope=TargetScope::Ranges;request.roi=RoiPolicy::WholePlane;request.t=request.c=AxisRange{0,1,1};request.z=AxisRange{0,int(planes.size()),1};const auto target=resolve_target(owner.snapshot(),request);
  std::string operation=row["operation"],name;OperationParameters parameters;
  const bool rank=operation=="minimum"||operation=="maximum"||operation=="mean"||operation=="median";
  if(rank){name="rank_filter_imagej";parameters={{"method",operation},{"radius",1.}};}
  else name=operation;
  if(!row["error"].is_null()){rejects([&]{prepare_operation(name,target,parameters);});check(reads.counts().plane_reads==0,"Indexed invalid type sampled input");continue;}
  auto prepared=prepare_operation(name,target,parameters);if(rank)for(int z=0;z<int(planes.size());++z)check(prepared.preview().data().dataset.plane({0,0,z})->pixels==row["output"][z].get<std::vector<double>>(),"Indexed type/filter pixel mismatch");
 }
}
void variance_references(const char *path){
 std::ifstream file(path);Json reference;file>>reference;check(reference.at("cases").size()==720,"Indexed type fixture count");
 for(const auto &row:reference.at("cases")){
  IndexedPalette palette;palette.color_count=int(row["source_palette"].size());for(int i=0;i<palette.color_count;++i)for(int c=0;c<3;++c)palette.palette[i][c]=row["source_palette"][i][c].get<unsigned char>();
  const auto kind=classify_indexed_palette(palette);check(kind.color==row["before"]["color"]&&kind.pseudo==row["before"]["pseudo"]&&kind.inverted==row["before"]["inverted"],"Indexed LUT classification");
  Stack planes;for(const auto &values:row["input"]){Image image(8,4,1,PixelType::UInt8);image.pixels=values.get<std::vector<double>>();planes.push_back(image);}
  DatasetReadObserver reads;Document owner(Dataset::from_planes({1,1,int(planes.size()),4,8,1},planes).observe_reads(reads));auto edit=owner.begin_edit();const auto encoded=encode_indexed_palette(palette);edit.set_extension(indexed_palette_extension,metadata_object(encoded));if(row["rectangle"]==1)edit.select(Selection("rectangle",{{1,1},{6,3}}));owner.commit(edit);
  auto display=owner.snapshot().display();display.crosshair[2]=int(planes.size())-1;display.properties["custom_lut"]=encoded.at("palette");if(row["range"]==1)display.properties["display_range"]=metadata_array({1.,127.});owner.set_display(display);
  TargetRequest request;request.scope=TargetScope::Ranges;request.roi=row["rectangle"]==1?RoiPolicy::ActiveSelection:RoiPolicy::WholePlane;request.t=request.c=AxisRange{0,1,1};request.z=AxisRange{0,int(planes.size()),1};const auto target=resolve_target(owner.snapshot(),request);
  std::string operation=row["operation"],name;OperationParameters parameters;
  name="rank_filter_imagej";parameters={{"method",std::string("variance")},{"radius",row["radius"].get<double>()}};
  if(!row["error"].is_null()){rejects([&]{prepare_operation(name,target,parameters);});check(reads.counts().plane_reads==0,"Indexed invalid type sampled input");continue;}
  auto prepared=prepare_operation(name,target,parameters);check(indexed_palette(prepared.preview().data())->color_count==palette.color_count,"Variance palette retention");check(metadata_equal(prepared.preview().display().properties,owner.snapshot().display().properties),"Variance display retention");for(int z=0;z<int(planes.size());++z)check(prepared.preview().data().dataset.plane({0,0,z})->pixels==row["output"][z].get<std::vector<double>>(),"Indexed type/filter pixel mismatch");
 }
}
}
int main(int argc,char **argv){try{check(argc==5,"Indexed fixture arguments required");references(argv[1]);contracts();dataset_contracts();type_references(argv[2]);filter_references(argv[3]);variance_references(argv[4]);std::cout<<"Indexed reference and resource contracts passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
