#include <imagejxx/playback.hpp>
#include <climits>
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace imagejxx;
void check(bool value){if(!value)throw std::runtime_error("Playback sequence mismatch");}
int main(){try{
  check(playback_interval_seconds(8)==.125);
  check(std::abs(playback_interval_seconds(60,"calibrated_time",25,"ms",2)-.05)<1e-12);
  check(playback_interval_seconds(60,"calibrated_time",2,"min",3)==360);
  for(int kind=0;kind<5;++kind){bool rejected=false;try{
    if(kind==0)playback_interval_seconds(0);
    else if(kind==1)playback_interval_seconds(8,"calibrated_time",1,"frame");
    else if(kind==2)playback_interval_seconds(8,"calibrated_time",std::numeric_limits<double>::max(),"day",2);
    else if(kind==3)playback_interval_seconds(8,"calibrated_time",0);
    else playback_interval_seconds(8,"invalid");
  }catch(const std::invalid_argument&){rejected=true;}check(rejected);}
  for(auto mode:{PlaybackMode::Forward,PlaybackMode::Reverse,PlaybackMode::PingPong}){
    PlaybackSequence s(10,1,8,3,mode);check(s.count()==3&&s.last_selected()==7);
    std::vector<int> expected=mode==PlaybackMode::Forward?std::vector<int>{4,7,1,4,7,1}:
        mode==PlaybackMode::Reverse?std::vector<int>{7,4,1,7,4,1}:std::vector<int>{4,7,4,1,4,7};
    int position=1;bool reverse=false;
    for(int value:expected){auto next=s.advance(position,reverse);check(next.position==value);position=next.position;reverse=next.reverse;}
    auto entered=s.advance(9);check(entered.position==(mode==PlaybackMode::Reverse?7:1));
  }
  PlaybackSequence large(INT_MAX,0,INT_MAX-1,INT_MAX-1);check(large.count()==2);check(large.advance(0).position==INT_MAX-1);check(large.advance(INT_MAX-1).position==0);
  check(PlaybackSequence(1).advance(0).position==0);
  for(int kind=0;kind<6;++kind){bool rejected=false;try{
    if(kind==0)PlaybackSequence(0);else if(kind==1)PlaybackSequence(4,-1);else if(kind==2)PlaybackSequence(4,3,2);
    else if(kind==3)PlaybackSequence(4,0,4);else if(kind==4)PlaybackSequence(4,0,3,0);else PlaybackSequence(4).advance(4);
  }catch(const std::logic_error&){rejected=true;}check(rejected);}
  std::cout<<"Playback ranges, modes, off-grid entry and integer limits passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
