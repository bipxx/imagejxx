#include <imagejxx/application.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace imagejxx;
  Image pixels(8, 6, 1, PixelType::UInt8);
  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 8; ++x) pixels.at(x, y) = y * 8 + x;
  auto owner = std::make_shared<Document>(Dataset::from_stack({std::move(pixels)}));
  Application application;
  application.adopt(owner);
  const auto before = owner->snapshot();
  ChannelDisplayState initial;
  initial.mode = ChannelDisplayMode::Color;
  initial.channels.resize(1);
  auto job = application.submit_seeded_auto_contrast(initial);
  if (!job.wait_for(std::chrono::seconds(5)))
    throw std::runtime_error("Auto contrast deadline");
  if (owner->snapshot().display().properties.count("channel_display"))
    throw std::runtime_error("Unexpected publication before explicit publish");
  const auto command = application.publish_auto_contrast(job);
  if (command.applied_range() != std::array<double, 2>{0, 47} ||
      application.auto_contrast_divisor() != 5000 ||
      !before.shares_data(owner->snapshot()) || owner->snapshot().dirty())
    throw std::runtime_error("Unexpected Auto result or source mutation");
  std::cout << "PASS installed seeded Auto: range 0..47, cycle 5000, source unchanged\n";
}
