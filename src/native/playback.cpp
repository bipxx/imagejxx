#include <imagejxx/playback.hpp>
#include <cstdint>
#include <stdexcept>

namespace imagejxx {
PlaybackMode parse_playback_mode(const std::string &name) {
  if(name=="forward")return PlaybackMode::Forward;
  if(name=="reverse")return PlaybackMode::Reverse;
  if(name=="ping_pong")return PlaybackMode::PingPong;
  throw std::invalid_argument("Playback mode must be forward, reverse or ping_pong");
}
const char *playback_mode_name(PlaybackMode mode) {
  switch(mode) {
  case PlaybackMode::Forward:return "forward";
  case PlaybackMode::Reverse:return "reverse";
  case PlaybackMode::PingPong:return "ping_pong";
  }
  throw std::invalid_argument("Unknown playback mode");
}
PlaybackSequence::PlaybackSequence(int extent,int first,std::optional<int> last,
                                 int step,PlaybackMode mode)
    :extent_(extent),first_(first),last_(last.value_or(extent>0?extent-1:0)),step_(step),count_(0),mode_(mode) {
  (void)playback_mode_name(mode);
  if(extent<=0 || first<0 || first>last_ || last_>=extent || step<=0)
    throw std::invalid_argument("Playback requires 0 <= first <= last < extent and a positive step");
  count_=static_cast<int>((std::int64_t(last_)-first)/step+1);
}
int PlaybackSequence::last_selected() const {
  return static_cast<int>(std::int64_t(first_)+std::int64_t(count_-1)*step_);
}
PlaybackStep PlaybackSequence::advance(int position,bool reverse) const {
  if(position<0 || position>=extent_)throw std::out_of_range("Playback position is outside its extent");
  const bool backwards=mode_==PlaybackMode::Reverse || (mode_==PlaybackMode::PingPong && reverse);
  if(count_==1)return {first_,backwards};
  if(position<first_ || position>last_selected() || (position-first_)%step_!=0)
    return {backwards?last_selected():first_,backwards};
  int index=(position-first_)/step_;
  bool nextReverse=backwards;
  if(mode_==PlaybackMode::PingPong) {
    if(index==count_-1)nextReverse=true;
    else if(index==0)nextReverse=false;
    index+=nextReverse?-1:1;
  } else if(backwards) index=index==0?count_-1:index-1;
  else index=index==count_-1?0:index+1;
  return {static_cast<int>(std::int64_t(first_)+std::int64_t(index)*step_),nextReverse};
}
}
