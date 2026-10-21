#include <imagejxx/io.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace imagejxx;using Json=nlohmann::json;
namespace {
std::size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"Expected UInt8 refusal");}
void expect(const Dataset &data,const std::string &raw){
 check(data.pixel_type()==PixelType::UInt8 && data.sample_policy()==FloatingSamplePolicy::FiniteOnly,"Wrong byte descriptor");
 auto shape=data.shape();std::size_t i=0;
 for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){auto p=data.plane({t,c,z});for(auto v:p->pixels){check(v==std::stoul(raw.substr(i*2,2),nullptr,16),"Byte sample changed");++i;}}
 check(i*2==raw.size(),"Wrong byte sample count");
}
}
int main(int argc,char **argv){try{
 check(argc==3,"Supply fixture/output paths");std::filesystem::path fixtures=argv[1],output=argv[2];std::filesystem::create_directories(output.parent_path());check(std::filesystem::create_directory(output),"Fresh byte output root required");
 std::ifstream stream(fixtures/"SOURCE.json");Json manifest=Json::parse(stream);std::size_t valid=0;
 for(const auto &row:manifest.at("files")){
  auto path=(fixtures/row.at("file").get<std::string>()).u8string();
  if(!row.at("valid").get<bool>()){rejects([&]{inspect_image_file(path);});continue;}
  ++valid;auto info=inspect_image_file(path);check(info.format==ImageFileFormat::ScientificTiffU8V1 && info.series[0].pixel_type==PixelType::UInt8,"Wrong byte format");
  auto read=read_image_series(path);auto raw=row.at("hex").get<std::string>();expect(read.dataset,raw);
  check(read.metadata==info.series[0].metadata,"Byte inspection changed metadata");
  for(auto policy:{FloatingSamplePolicy::PreserveQuietNaNFloat32,FloatingSamplePolicy::PreserveIEEEFloat32}){ImageReadRequest request;request.sample_policy=policy;rejects([&]{read_image_series(path,request);});}
  ImageWriteRequest request;request.format=ImageFileFormat::ScientificTiffU8V1;
  DatasetReadObserver observer;auto data=read.dataset.observe_reads(observer);const auto estimate=estimate_image_write(data,read.metadata,request);
  check(observer.counts().plane_reads==0,"Byte planning read samples");
  auto target=(output/row.at("file").get<std::string>()).u8string();write_image_series(target,data,read.metadata,request);
  check(std::filesystem::file_size(target)==estimate.encoded_upper_bound,"Wrong byte output estimate");expect(read_image_series(target).dataset,raw);
  rejects([&]{write_image_series(target,data,{},request);});request.replace=FileReplacePolicy::ReplaceExisting;write_image_series(target,data,{},request);expect(read_image_series(target).dataset,raw);
  rejects([&]{estimate_image_write(data);});
 }
 check(valid==10,"Byte fixture inventory changed");std::cout<<checks<<" UInt8 TIFF checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
