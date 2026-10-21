#pragma once
#include <optional>
#include <string>

namespace imagejxx {
enum class PlaybackMode { Forward, Reverse, PingPong };
PlaybackMode parse_playback_mode(const std::string &name);
const char *playback_mode_name(PlaybackMode mode);
// FPS ignores calibration. Calibrated time includes the selected T stride.
// Loop boundaries have the same interval as ordinary selected positions.
double playback_interval_seconds(double fps, const std::string &timing = "fps",
                                 double frame_interval = 1,
                                 const std::string &time_unit = "s", int step = 1);
struct PlaybackStep {
  int position;
  bool reverse;
};
// Inclusive zero-based bounds. Strides start at first; last need not be on the
// grid. No storage proportional to the number of positions is allocated.
class PlaybackSequence {
public:
  PlaybackSequence(int extent, int first = 0, std::optional<int> last = {},
                   int step = 1, PlaybackMode mode = PlaybackMode::Forward);
  int extent() const { return extent_; }
  int first() const { return first_; }
  int last() const { return last_; }
  int step() const { return step_; }
  int count() const { return count_; }
  int last_selected() const;
  PlaybackMode mode() const { return mode_; }
  // An in-bounds off-grid current position enters at the appropriate end.
  // Ping-pong reverses without duplicating either endpoint. Pass the returned
  // reverse flag into the next call; the sequence object itself is immutable.
  PlaybackStep advance(int position, bool reverse = false) const;
private:
  int extent_, first_, last_, step_, count_;
  PlaybackMode mode_;
};
}
