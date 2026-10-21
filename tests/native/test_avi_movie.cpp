#include <imagejxx/avi_movie.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
using namespace imagejxx;
void check(bool v){if(!v)throw std::runtime_error("AVI check failed");}
std::vector<unsigned char> bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::uint32_t word(const std::vector<unsigned char> &b,std::size_t n){return b.at(n)|(std::uint32_t(b.at(n+1))<<8)|(std::uint32_t(b.at(n+2))<<16)|(std::uint32_t(b.at(n+3))<<24);}
int main(int argc,char **argv){try{
  if(argc!=2)throw std::invalid_argument("Fresh test directory required");
  const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));check(std::filesystem::create_directories(root));
  AviMovieSpec s{2,2,2,1,7.5};const auto e=estimate_avi_movie(s);check(e.rate==15&&e.scale==2);
  int calls=0;auto provider=[&](std::size_t f){++calls;Image im(2,2,1,PixelType::UInt8);im.pixels={double(f+1),double(f+2),double(f+3),double(f+4)};return im;};
  auto stage=stage_avi_movie((root/"gray.avi").u8string(),s,provider);check(calls==2&&!std::filesystem::exists(root/"gray.avi"));stage->publish();
  const auto b=bytes(root/"gray.avi");check(b.size()==e.file_bytes&&word(b,4)==b.size()-8);check(b.at(1256)==3&&b.at(1257)==4&&b.at(1260)==1&&b.at(1261)==2);
  check(word(b,b.size()-8)==4+8+e.frame_bytes);
  calls=0;s.max_file_bytes=1;bool refused=false;try{write_avi_movie((root/"refuse.avi").u8string(),s,provider);}catch(const std::invalid_argument&){refused=true;}check(refused&&calls==0&&!std::filesystem::exists(root/"refuse.avi"));s.max_file_bytes=1<<20;
  const auto before=bytes(root/"gray.avi");refused=false;try{write_avi_movie((root/"gray.avi").u8string(),s,provider);}catch(const std::exception&){refused=true;}check(refused&&bytes(root/"gray.avi")==before);
  ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.stage_progress=[&](std::string_view name,std::size_t done,std::size_t){if(name=="AVI frames"&&done==1)control.cancellation->cancel();};
  refused=false;try{write_avi_movie((root/"gray.avi").u8string(),s,provider,true,control);}catch(const OperationCancelled&){refused=true;}check(refused&&bytes(root/"gray.avi")==before);
  int files=0;for(const auto &entry:std::filesystem::directory_iterator(root)){(void)entry;++files;}check(files==1);
  std::cout<<"AVI row/index bytes, staging, limits, overwrite and cancellation passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
