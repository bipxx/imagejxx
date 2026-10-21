#include <imagejxx/avi_movie.hpp>
#include <iostream>
int main(int argc,char **argv){
  if(argc!=2)return 2;
  try{
    imagejxx::Stack frames;
    for(int z=0;z<3;++z){imagejxx::Image image(3,2,3,imagejxx::PixelType::UInt8);for(int y=0;y<2;++y)for(int x=0;x<3;++x)for(int c=0;c<3;++c)image.at(x,y,c)=z*50+y*10+x*3+c;frames.push_back(std::move(image));}
    imagejxx::write_avi_dataset(argv[1],imagejxx::Dataset::from_stack(frames),7.5);
    std::cout<<"Wrote uncompressed RGB AVI from the installed SDK\n";return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
