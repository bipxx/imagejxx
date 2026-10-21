#include <imagejxx/io.hpp>
#include <nlohmann/json.hpp>
#include "../../src/io/internal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace imagejxx;
namespace {
std::size_t checks=0;
void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}check(failed,"Expected predictor refusal");}
void expect(const Dataset &data,const std::string &hex){
 const auto shape=data.shape();std::size_t at=0;
 for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
  for(auto v:data.plane({t,c,z})->pixels){const auto word=float32_source_sample_bits(v,data.sample_policy());for(int b=0;b<4;++b){
   check(((word>>(b*8))&255)==std::stoul(hex.substr(at,2),nullptr,16),"Floating predictor changed sample word");at+=2;
  }}
 check(at==hex.size(),"Floating predictor sample count changed");
}
}
int main(int argc,char **argv){try{
 check(argc==4,"Supply fixtures/output");std::filesystem::path root=argv[1],out=argv[2];std::filesystem::create_directories(out.parent_path());check(std::filesystem::create_directory(out),"Fresh output required");
 std::ifstream f(root/"SOURCE.json");const auto manifest=nlohmann::json::parse(f);std::size_t valid=0,bad=0;
 for(const auto &row:manifest.at("files")){
  const auto path=(root/row.at("file").get<std::string>()).u8string();
  if(!row.at("valid").get<bool>()){++bad;rejects([&]{inspect_image_file(path);});rejects([&]{read_image_series(path);});continue;}
  ++valid;ImageReadRequest read;const auto pattern=row.at("pattern").get<std::string>();
  read.sample_policy=pattern=="finite"?FloatingSamplePolicy::FiniteOnly:pattern=="quiet"?FloatingSamplePolicy::PreserveQuietNaNFloat32:FloatingSamplePolicy::PreserveIEEEFloat32;
  const auto profile=io_detail::image_profile(PixelType::Float32,1,row.at("codec").get<unsigned>(),3);
  auto info=inspect_image_file(path);check(info.format==profile.format,"Wrong predictor format");
  const auto oracle=(std::filesystem::path(argv[3])/row.at("file").get<std::string>()).u8string();expect(read_image_series(oracle,read).dataset,row.at("hex").get<std::string>());
  auto result=read_image_series(path,read);const auto hex=row.at("hex").get<std::string>();expect(result.dataset,hex);
  check(result.metadata==info.series[0].metadata,"Predictor metadata changed");
  check(result.dataset.calibration().spacing==std::array<double,3>{.5,1.25,2.75},"Predictor calibration changed");
  DatasetReadObserver observer;auto observed=result.dataset.observe_reads(observer);ImageWriteRequest write;write.format=profile.format;
  auto estimate=estimate_image_write(observed,result.metadata,write);check(observer.counts().plane_reads==0,"Predictor planning read samples");
  const auto target=(out/row.at("file").get<std::string>()).u8string();write_image_series(target,observed,result.metadata,write);
  check(std::filesystem::file_size(target)<=estimate.encoded_upper_bound,"Predictor exceeded estimate");
  check(inspect_image_file(target).format==profile.format,"Writer dropped predictor tag");expect(read_image_series(target,read).dataset,hex);
  rejects([&]{write_image_series(target,observed,{},write);});write.replace=FileReplacePolicy::ReplaceExisting;write_image_series(target,observed,{},write);expect(read_image_series(target,read).dataset,hex);
  write.format=ImageFileFormat::ScientificTiffFloat32V1;
  const auto raw=(out/(row.at("file").get<std::string>()+"-raw.tif")).u8string();write_image_series(raw,observed,result.metadata,write);expect(read_image_series(raw,read).dataset,hex);
 }
 check(valid==78 && bad==6,"Predictor corpus changed");
 // A fixed byte vector checks plane shuffle and cross-plane differencing.
 for(bool little:{false,true}){
  std::vector<unsigned char> raw=little?std::vector<unsigned char>{4,3,2,1,20,19,18,17}:std::vector<unsigned char>{1,2,3,4,17,18,19,20};
  auto transformed=raw;std::vector<unsigned char> scratch(raw.size());
  io_detail::floating_predict(transformed.data(),scratch.data(),raw.size(),little,false,{});
  check(transformed==std::vector<unsigned char>{1,16,241,16,241,16,241,16},"Floating predictor known vector");
  io_detail::floating_predict(transformed.data(),scratch.data(),raw.size(),little,true,{});check(transformed==raw,"Floating predictor inverse vector");
 }
 for(bool decode:{false,true}){std::vector<unsigned char> row(262144,1),scratch(row.size());unsigned calls=0;ExecutionControl control;control.checkpoint=[&]{if(++calls==2)throw OperationCancelled();};
  rejects([&]{io_detail::floating_predict(row.data(),scratch.data(),row.size(),true,decode,control);});check(calls==2,"Floating predictor mid-row cancellation");}
 std::cout<<checks<<" floating TIFF checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
