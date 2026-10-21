#include <imagejxx/io.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
std::size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"Expected refusal");}
std::uint32_t word(const std::string &raw,std::size_t i){std::uint32_t value=0;for(int b=0;b<4;++b)value|=std::uint32_t(std::stoul(raw.substr(8*i+2*b,2),nullptr,16))<<(8*b);return value;}
void expect(const Dataset &data,const std::string &raw){
 check(data.pixel_type()==PixelType::Float32,"Wrong Float32 descriptor");
 std::size_t i=0;auto shape=data.shape();
 for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){auto p=data.plane({t,c,z});for(double v:p->pixels)check(float32_source_sample_bits(v,data.sample_policy())==word(raw,i++),"Float32 word changed");}
 check(i*8==raw.size(),"Wrong sample count");
}
}
int main(int argc,char **argv){try{
 check(argc==3,"Supply fixture/output directories");std::filesystem::path fixtures=argv[1],output=argv[2];std::filesystem::create_directories(output.parent_path());check(std::filesystem::create_directory(output),"Output root must be fresh");
 std::ifstream stream(fixtures/"SOURCE.json");Json manifest=Json::parse(stream);std::size_t valid=0;
 for(const auto &row:manifest.at("files")){
  auto path=(fixtures/row.at("file").get<std::string>()).u8string();
  if(!row.at("valid").get<bool>()){rejects([&]{inspect_image_file(path);});continue;}
  ++valid;auto info=inspect_image_file(path);check(info.format==ImageFileFormat::ScientificTiffFloat32V1,"Wrong format");check(info.series[0].sample_policy==FloatingSamplePolicy::PreserveIEEEFloat32,"Inspection must not claim finite samples without decoding");
  ImageReadRequest request;request.sample_policy=FloatingSamplePolicy::PreserveIEEEFloat32;
  auto read=read_image_series(path,request);auto raw=row.at("words_hex").get<std::string>();expect(read.dataset,raw);
  check(read.metadata==info.series[0].metadata,"Inspection metadata changed");
  if(row.at("group")=="finite")expect(read_image_series(path).dataset,raw);else rejects([&]{read_image_series(path);});
  request.sample_policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
  if(row.at("group")!="ieee")expect(read_image_series(path,request).dataset,raw);else rejects([&]{read_image_series(path,request);});
  ImageWriteRequest write;write.format=ImageFileFormat::ScientificTiffFloat32V1;
  auto target=(output/row.at("file").get<std::string>()).u8string();
  const auto estimate=estimate_image_write(read.dataset,read.metadata,write);
  write_image_series(target,read.dataset,read.metadata,write);
  check(std::filesystem::file_size(target)==estimate.encoded_upper_bound,"Wrong encoded size");
  request.sample_policy=FloatingSamplePolicy::PreserveIEEEFloat32;expect(read_image_series(target,request).dataset,raw);
  rejects([&]{write_image_series(target,read.dataset,{},write);});
  write.replace=FileReplacePolicy::ReplaceExisting;write_image_series(target,read.dataset,{},write);expect(read_image_series(target,request).dataset,raw);
  rejects([&]{estimate_image_write(read.dataset);});
  auto canceled=std::make_shared<CancellationToken>();canceled->cancel();ExecutionControl control;control.cancellation=canceled;
  rejects([&]{read_image_series(path,request,{},control);});
  auto canceled_path=(output/(row.at("file").get<std::string>()+".cancel")).u8string();rejects([&]{write_image_series(canceled_path,read.dataset,{},write,{},control);});check(!std::filesystem::exists(canceled_path),"Canceled output published");
 }
 check(valid==15,"Fixture inventory changed");std::cout<<checks<<" Float32 TIFF checks passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<"\n";return 1;}}
