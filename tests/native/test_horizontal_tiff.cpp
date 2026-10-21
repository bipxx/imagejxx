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
 const auto shape=data.shape();std::size_t at=0;const auto size=data.pixel_type()==PixelType::UInt16?2:1;
 for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
  for(auto v:data.plane({t,c,z})->pixels)for(int b=0;b<size;++b){
   check(((unsigned(v)>>(b*8))&255)==std::stoul(hex.substr(at,2),nullptr,16),"Predictor changed sample word");at+=2;
  }
 check(at==hex.size(),"Predictor sample count changed");
}
}
int main(int argc,char **argv){try{
 check(argc==3,"Supply fixtures/output");std::filesystem::path root=argv[1],out=argv[2];std::filesystem::create_directories(out.parent_path());check(std::filesystem::create_directory(out),"Fresh output required");
 std::ifstream f(root/"SOURCE.json");const auto manifest=nlohmann::json::parse(f);std::size_t valid=0,bad=0;
 for(const auto &row:manifest.at("files")){
  const auto path=(root/row.at("file").get<std::string>()).u8string();
  if(!row.at("valid").get<bool>()){++bad;rejects([&]{inspect_image_file(path);});rejects([&]{read_image_series(path);});continue;}
  ++valid;const auto dtype=row.at("dtype").get<std::string>();const auto type=dtype=="u16"?PixelType::UInt16:PixelType::UInt8;
  const auto profile=io_detail::image_profile(type,dtype=="rgb8"?3:1,row.at("codec").get<unsigned>(),2);
  auto info=inspect_image_file(path);check(info.format==profile.format,"Wrong predictor format");
  auto result=read_image_series(path);const auto hex=row.at("hex").get<std::string>();expect(result.dataset,hex);
  check(result.metadata==info.series[0].metadata,"Predictor metadata changed");
  check(result.dataset.calibration().spacing==std::array<double,3>{.5,1.25,2.75},"Predictor calibration changed");
  DatasetReadObserver observer;auto observed=result.dataset.observe_reads(observer);ImageWriteRequest write;write.format=profile.format;
  auto estimate=estimate_image_write(observed,result.metadata,write);check(observer.counts().plane_reads==0,"Predictor planning read samples");
  const auto target=(out/row.at("file").get<std::string>()).u8string();write_image_series(target,observed,result.metadata,write);
  check(std::filesystem::file_size(target)<=estimate.encoded_upper_bound,"Predictor exceeded estimate");
  check(inspect_image_file(target).format==profile.format,"Writer dropped predictor tag");expect(read_image_series(target).dataset,hex);
  rejects([&]{write_image_series(target,observed,{},write);});write.replace=FileReplacePolicy::ReplaceExisting;write_image_series(target,observed,{},write);expect(read_image_series(target).dataset,hex);
  write.format=io_detail::image_profile(type,dtype=="rgb8"?3:1,1).format;
  const auto raw=(out/(row.at("file").get<std::string>()+"-raw.tif")).u8string();write_image_series(raw,observed,result.metadata,write);expect(read_image_series(raw).dataset,hex);
 }
 check(valid==84 && bad==6,"Predictor corpus changed");
 // Independent numerical vectors cover endian order, modulo wrap and RGB stride.
 for(bool little:{false,true})for(std::size_t size:{1u,2u})for(std::size_t components:{1u,3u}){
  const unsigned mask=size==1?255:65535;std::vector<unsigned> words{mask,1,mask-1,0,2,mask,1,0,3};
  if(components==1)words.resize(7);
  std::vector<unsigned char> raw;
  auto append=[&](std::vector<unsigned char> &v,unsigned word){if(size==1)v.push_back(static_cast<unsigned char>(word));else if(little){v.push_back(word&255);v.push_back(word>>8);}else{v.push_back(word>>8);v.push_back(word&255);}};
  for(auto word:words)append(raw,word);
  std::vector<unsigned char> expected;for(std::size_t i=0;i<words.size();++i)append(expected,(words[i]-(i<components?0:words[i-components]))&mask);
  auto transformed=raw;io_detail::horizontal_predict(transformed.data(),transformed.size(),size,components,little,false,{});check(transformed==expected,"Predictor vector encode");
  io_detail::horizontal_predict(transformed.data(),transformed.size(),size,components,little,true,{});check(transformed==raw,"Predictor vector decode");
 }
 for(bool decode:{false,true}){std::vector<unsigned char> row(262144,1);unsigned calls=0;ExecutionControl control;control.checkpoint=[&]{if(++calls==2)throw OperationCancelled();};
  rejects([&]{io_detail::horizontal_predict(row.data(),row.size(),1,1,true,decode,control);});check(calls==2,"Predictor mid-row cancellation");}
 std::cout<<checks<<" horizontal TIFF checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
