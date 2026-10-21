#include "roi_import_limits.hpp"
#include "../native/imagej_annotation_import_internal.hpp"
#include <charconv>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
namespace imagejxx::desktop {
namespace {
const char *labels[]{"Batch members",
                     "Batch input bytes",
                     "Source archive bytes",
                     "Decoded batch bytes",
                     "ROI wire bytes",
                     "ROI vertices",
                     "ROI text bytes",
                     "Mask pixels",
                     "Path work units",
                     "Wire image pixels",
                     "Display retained bytes",
                     "ZIP archive bytes",
                     "ZIP members",
                     "ZIP member bytes",
                     "ZIP total member bytes",
                     "ZIP name bytes",
                     "ZIP working bytes"};
const char *keys[]{"members",     "input",      "archive",   "decoded",
                   "wire",        "vertices",   "text",      "mask",
                   "path_work",   "pixels",     "display",   "zip_archive",
                   "zip_members", "zip_member", "zip_total", "zip_name",
                   "zip_working"};
constexpr std::array<std::size_t, 17> minimum{1, 1,    1,  1, 64, 1, 1, 1, 1,
                                              1, 4096, 22, 1, 1,  1, 5, 1};
constexpr std::array<std::size_t, 17> maximum{
    4096,          64 * 1048576, 64 * 1048576, 128 * 1048576, 64 * 1048576,
    65536,         64 * 1048576, 16 * 1048576, 64 * 1048576,  4 * 1048576,
    256 * 1048576, 64 * 1048576, 4096,         64 * 1048576,  64 * 1048576,
    1024,          192 * 1048576};
std::array<std::size_t *, 17> pointers(RoiImportLimitValues &v) {
  auto &l = v.annotations.batch;
  auto &z = v.zip;
  return {&l.max_entries,
          &l.max_total_bytes,
          &l.max_provenance_bytes,
          &l.max_decoded_bytes,
          &l.roi.max_bytes,
          &l.roi.max_vertices,
          &l.roi.max_text_bytes,
          &l.roi.max_mask_pixels,
          &l.roi.max_path_work,
          &l.roi.max_image_pixels,
          &v.annotations.max_display_retained_bytes,
          &z.max_archive_bytes,
          &z.max_entries,
          &z.max_entry_bytes,
          &z.max_total_bytes,
          &z.max_name_bytes,
          &z.max_working_bytes};
}
void validate(const RoiImportLimitValues &v) {
  detail::validate_imagej_annotation_import_limits(v.annotations);
  const auto &z = v.zip;
  if (z.max_archive_bytes < 22 || z.max_archive_bytes > 64 * 1048576 ||
      !z.max_entries || z.max_entries > 4096 || !z.max_entry_bytes ||
      z.max_entry_bytes > 64 * 1048576 || !z.max_total_bytes ||
      z.max_total_bytes > 64 * 1048576 || z.max_name_bytes < 5 ||
      z.max_name_bytes > 1024 || !z.max_working_bytes ||
      z.max_working_bytes > 192 * 1048576)
    throw std::invalid_argument(
        "ZIP limits exceed the supported import bounds");
}
} // namespace
RoiImportLimitEditor::RoiImportLimitEditor(const RoiImportLimitValues &v) {
  load(v);
}
void RoiImportLimitEditor::load(const RoiImportLimitValues &v) {
  auto copy = v;
  const auto ptr = pointers(copy);
  for (std::size_t i = 0; i < fields_.size(); ++i)
    fields_[i] = std::to_string(*ptr[i]);
  error_.clear();
}
int RoiImportLimitEditor::draw(RoiImportLimitValues &out) {
  controls_ = Json::object();
  if (focus_)
    ImGui::SetNextWindowFocus();
  focus_ = false;
  ImGui::SetNextWindowSize({560, 630}, ImGuiCond_FirstUseEver);
  bool open = true;
  ImGui::Begin("ImageJ ROI import limits", &open);
  const auto a = ImGui::GetCurrentWindow()->InnerRect.Min,
             b = ImGui::GetCurrentWindow()->InnerRect.Max;
  bounds_ = {a.x, a.y, b.x, b.y};
  auto observe = [&](const std::string &key, float width = 0) {
    auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls_[key] = {
        {"id", ImGui::GetItemID()},
        {"enabled", true},
        {"visible", ImGui::IsItemVisible()},
        {"rect", {a.x, a.y, b.x, b.y}},
        {"point",
         {width ? a.x + width / 2 : (a.x + b.x) / 2, (a.y + b.y) / 2}}};
  };
  ImGui::TextWrapped(
      "Exact byte and count limits. ZIP and batch caps both apply. Lower "
      "limits may refuse otherwise valid files; changes require a new review.");
  if (ImGui::Button("Default limits"))
    load({});
  observe("default");
  ImGui::SameLine();
  if (ImGui::Button("Large input preset")) {
    RoiImportLimitValues v;
    auto &l = v.annotations.batch;
    l.max_total_bytes = l.max_provenance_bytes = 64 * 1048576;
    l.max_decoded_bytes = 128 * 1048576;
    l.roi.max_bytes = l.roi.max_text_bytes = 64 * 1048576;
    v.annotations.max_display_retained_bytes = 256 * 1048576;
    v.zip.max_entry_bytes = 64 * 1048576;
    load(v);
  }
  observe("large");
  ImGui::BeginChild(
      "Limit fields",
      {0, std::max(100.f, ImGui::GetContentRegionAvail().y - 100)}, true);
  const auto cp = ImGui::GetWindowPos(), cs = ImGui::GetWindowSize();
  for (std::size_t i = 0; i < fields_.size(); ++i) {
    ImGui::PushID(int(i));
    ImGui::SetNextItemWidth(180);
    ImGui::InputText(labels[i], &fields_[i], ImGuiInputTextFlags_CharsDecimal);
    observe(keys[i], 180);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Supported range: %llu to %llu%s",
          static_cast<unsigned long long>(minimum[i]),
          static_cast<unsigned long long>(maximum[i]),
          i == 9 ? "\nNative display payloads also have a 1048576-pixel cap."
                 : "");
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  int result = 0;
  if (ImGui::Button("Apply import limits"))
    try {
      RoiImportLimitValues values;
      auto ptr = pointers(values);
      for (std::size_t i = 0; i < fields_.size(); ++i) {
        const auto &s = fields_[i];
        std::size_t n = 0;
        auto p = std::from_chars(s.data(), s.data() + s.size(), n);
        if (s.empty() || s.size() > 20 || p.ec != std::errc{} ||
            p.ptr != s.data() + s.size())
          throw std::invalid_argument(
              std::string(labels[i]) +
              " requires a complete nonnegative integer");
        *ptr[i] = n;
      }
      validate(values);
      out = std::move(values);
      result = 1;
    } catch (const std::exception &e) {
      error_ = e.what();
    }
  observe("apply");
  ImGui::SameLine();
  if (ImGui::Button("Cancel limits"))
    result = -1;
  observe("cancel");
  if (!error_.empty())
    ImGui::TextWrapped("%s", error_.c_str());
  const bool escape =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape);
  ImGui::End();
  controls_["fields_window"] = {
      {"rect", {cp.x, cp.y, cp.x + cs.x, cp.y + cs.y}}};
  return !open || escape ? -1 : result;
}
Json RoiImportLimitEditor::diagnostics() const {
  Json fields = Json::object();
  for (std::size_t i = 0; i < fields_.size(); ++i)
    fields[keys[i]] = fields_[i];
  return {{"fields", fields},
          {"controls", controls_},
          {"window", bounds_},
          {"error", error_}};
}
} // namespace imagejxx::desktop
