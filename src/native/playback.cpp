#include <imagejxx/playback.hpp>
#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace imagejxx {
double playback_interval_seconds(double fps, const std::string &timing,
                                 double frame_interval, const std::string &unit, int step) {
  if(step<=0)throw std::invalid_argument("Playback step must be positive");
  double seconds;
  if(timing=="fps") {
    if(!std::isfinite(fps) || fps<=0)throw std::invalid_argument("Playback FPS must be finite and positive");
    seconds=1/fps;
  } else if(timing=="calibrated_time") {
    double scale;
    if(unit=="s" || unit=="sec" || unit=="second" || unit=="seconds")scale=1;
    else if(unit=="ms" || unit=="millisecond" || unit=="milliseconds")scale=.001;
    else if(unit=="us" || unit=="\xc2\xb5s" || unit=="\xce\xbcs" || unit=="microsecond" || unit=="microseconds")scale=.000001;
    else if(unit=="ns" || unit=="nanosecond" || unit=="nanoseconds")scale=.000000001;
    else if(unit=="min" || unit=="minute" || unit=="minutes")scale=60;
    else if(unit=="h" || unit=="hr" || unit=="hour" || unit=="hours")scale=3600;
    else if(unit=="d" || unit=="day" || unit=="days")scale=86400;
    else throw std::invalid_argument("Playback calibration needs a recognized time unit");
    if(!std::isfinite(frame_interval) || frame_interval<=0)
      throw std::invalid_argument("Playback frame interval must be finite and positive");
    seconds=frame_interval*(scale*step);
  } else throw std::invalid_argument("Playback timing must be fps or calibrated_time");
  if(!std::isfinite(seconds) || seconds<=0)
    throw std::invalid_argument("Playback interval is outside the supported seconds range");
  return seconds;
}
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
