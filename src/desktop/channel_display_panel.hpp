#pragma once

#include "desktop.hpp"
#include <imagejxx/channel_display_command.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace imagejxx::desktop {

// Modeless widgets only. The host owns admission, command resolution, atomic
// publication and error reporting. No document or pixel borrow survives draw().
class ChannelDisplayPanel {
public:
  using Enable = std::function<void(const DocumentSnapshot &)>;
  using Apply = std::function<bool(const DocumentSnapshot &, const ChannelDisplayRequest &)>;
  using SelectChannel = std::function<void(int)>; // Native zero-based C.
  using LoadLut = std::function<void()>;
  using Auto = std::function<void(const DocumentSnapshot &, bool all_channels,
                                  bool use_area_selection)>;
  bool autoUseSelection() const { return autoUseSelection_; }

  void open();
  void close();
  bool visible() const { return open_; }
  // Most recent panel focus or a currently active owned range text widget.
  // The host can suppress canvas shortcuts without retaining a document.
  bool inputActive() const;
  void draw(const DocumentSnapshot *current, bool busy,
            const Enable &enable, const Apply &apply,
            const SelectChannel &select_channel, const LoadLut &load_lut,
            const Auto &auto_contrast = {});
  Json diagnostics() const;

private:
  void resetControls();
  void control(const std::string &name, bool enabled,
               std::optional<float> input_width = std::nullopt);
  void updateRangeDraft(std::uint64_t owner, int channel,
                        std::optional<std::array<double, 2>> range);
  void parseRangeDraft();

  bool open_ = false, focus_ = false, busy_ = false;
  bool autoUseSelection_ = true;
  bool sourceValid_ = false, supported_ = false, propertyPresent_ = false;
  bool windowFocused_ = false, draftValid_ = false;
  std::uint64_t sourceId_ = 0, rangeOwner_ = 0;
  int t_ = 0, c_ = 0, z_ = 0, rangeChannel_ = -1;
  bool rangeInitialized_ = false, forceRangeRebase_ = false;
  std::optional<std::array<double, 2>> cachedRange_, draftRange_;
  std::string minimumText_, maximumText_, rangeError_, sourceError_, exitReason_, modeLabel_;
  std::uint32_t minimumId_ = 0, maximumId_ = 0;
  int focusField_ = 0;
  Json state_ = nullptr, shape_ = nullptr;
  Json controls_ = Json::object(), controlRects_ = Json::object();
  Json controlStates_ = Json::object();
};

} // namespace imagejxx::desktop
