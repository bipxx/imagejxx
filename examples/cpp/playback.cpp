#include <imagejxx/playback.hpp>
#include <stdexcept>
int main() {
  if(imagejxx::playback_interval_seconds(8)!=.125) return 2;
  if(imagejxx::playback_interval_seconds(60,"calibrated_time",25,"ms",2)!=.05) return 3;
  const imagejxx::PlaybackSequence sequence(10, 1, 8, 3, imagejxx::PlaybackMode::PingPong);
  int position=1;bool reverse=false;
  for(int expected:{4,7,4,1,4,7}) {
    const auto next=sequence.advance(position,reverse);
    if(next.position!=expected)throw std::runtime_error("Installed playback sequence mismatch");
    position=next.position;reverse=next.reverse;
  }
}
