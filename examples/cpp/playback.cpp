#include <imagejxx/playback.hpp>
#include <stdexcept>
int main() {
  const imagejxx::PlaybackSequence sequence(10, 1, 8, 3, imagejxx::PlaybackMode::PingPong);
  int position=1;bool reverse=false;
  for(int expected:{4,7,4,1,4,7}) {
    const auto next=sequence.advance(position,reverse);
    if(next.position!=expected)throw std::runtime_error("Installed playback sequence mismatch");
    position=next.position;reverse=next.reverse;
  }
}
