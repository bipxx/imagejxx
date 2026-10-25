#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace imagejxx;
using imagejxx::desktop::HeadlessInputSession;
using imagejxx::desktop::Json;
using imagejxx::desktop::Window;
namespace {
namespace fs = std::filesystem;
std::size_t checks = 0;
constexpr std::size_t page_size = 64;
constexpr std::size_t column_count = 1024;
constexpr std::size_t custom_count = 1020;
constexpr std::array<double, 30> source_words{
    0, 0, 0, 0, 0, 0,
    0, 2, 3, 4, 0, 0,
    0, 5, 6, 7, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0};
const std::vector<std::string> particle_names{"min", "max", "bbox", "pixels"};
const std::array<std::string, 4> particle_text{"2.0", "7.0", "[1,1,3,2]", "6"};
const std::string multiline_name = "custom##literal\nsecond";

void check(bool value, const std::string &message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

std::vector<std::string> wide_names() {
  std::vector<std::string> names;
  names.reserve(column_count);
  names.push_back("custom##literal");
  for (std::size_t i = 1; i < custom_count; ++i)
    names.push_back(i == 1 ? multiline_name : "custom_" + std::to_string(i));
  names.insert(names.end(), particle_names.begin(), particle_names.end());
  return names;
}

class Input {
public:
  Window &window;
  HeadlessInputSession session;
  Json last;
  Json scroll_trace = Json::array();
  fs::path diagnostic_path;
  std::size_t frames = 0, clicks = 0;
  explicit Input(Window &owner, const fs::path &directory)
      : window(owner), session(owner), diagnostic_path(directory / "scroll-observations.json") {
    frame(); frame(); frame();
  }
  void frame(const Json &events = Json::array()) {
    check(frames++ < 600, "Results pager exceeded its bounded actual-input frames");
    last = session.frame(events);
    check(last.at("input_frame").at("raw_inputs") == events,
          "Results input delivery differs from the requested physical events");
    check(window.error().empty(), "Results host error: " + window.error());
  }
  const Json &table() const { return last.at("results_table"); }
  static Json rect(const ImRect &value) {
    return {value.Min.x, value.Min.y, value.Max.x, value.Max.y};
  }
  static Json native_window(const ImGuiWindow *value) {
    if (!value) return Json();
    return {{"name", value->Name}, {"id", value->ID}, {"active", value->Active},
            {"hidden", value->Hidden}, {"flags", value->Flags},
            {"scroll", {value->Scroll.x, value->Scroll.y}},
            {"scroll_max", {value->ScrollMax.x, value->ScrollMax.y}},
            {"scroll_target", {value->ScrollTarget.x, value->ScrollTarget.y}},
            {"clip", rect(value->ClipRect)}, {"inner_rect", rect(value->InnerRect)},
            {"inner_clip", rect(value->InnerClipRect)}};
  }
  const ImGuiTable *native_table() const {
    auto &context = *ImGui::GetCurrentContext();
    const auto *results = ImGui::FindWindowByName("Results");
    for (int i = 0; i < context.Tables.GetMapSize(); ++i)
      if (const auto *value = context.Tables.TryGetMapData(i))
        if (value->OuterWindow == results && value->LastFrameActive == context.FrameCount)
          return value;
    return nullptr;
  }
  Json scroll_state(std::size_t global_column) const {
    const auto &context = *ImGui::GetCurrentContext();
    Json result{{"frame", frames}, {"page", table().at("page")},
        {"first_column", table().at("first_column")}, {"target_column", global_column},
        {"actual_mouse", {context.IO.MousePos.x, context.IO.MousePos.y}},
        {"hovered_id", context.HoveredId}, {"hovered_window", native_window(context.HoveredWindow)},
        {"wheeling_window", native_window(context.WheelingWindow)},
        {"results_window", native_window(ImGui::FindWindowByName("Results"))},
        {"header", Json()}, {"cell", Json()}, {"visible_headers", Json::array()}};
    for (const auto &header : table().at("headers")) {
      if (header.at("column") == global_column) result["header"] = header;
      if (header.at("visible").get<bool>()) result["visible_headers"].push_back(header.at("column"));
    }
    for (const auto &cell : table().at("cells"))
      if (cell.at("column") == global_column && cell.at("row") == 0) result["cell"] = cell;
    if (const auto *value = native_table()) {
      result["native_table"] = {{"id", value->ID}, {"columns", value->ColumnsCount},
          {"last_frame_active", value->LastFrameActive}, {"inner_rect", rect(value->InnerRect)},
          {"inner_clip", rect(value->InnerClipRect)}, {"outer_rect", rect(value->OuterRect)},
          {"freeze_columns", value->FreezeColumnsCount}, {"freeze_columns_request", value->FreezeColumnsRequest},
          {"current_row_y", {value->RowPosY1, value->RowPosY2}},
          {"inner_window", native_window(value->InnerWindow)}};
      const auto first = table().at("first_column").get<std::size_t>();
      if (global_column >= first && global_column - first < static_cast<std::size_t>(value->ColumnsCount)) {
        const auto &column = value->Columns[static_cast<int>(global_column - first)];
        result["native_target"] = {{"min_x", column.MinX}, {"max_x", column.MaxX},
            {"work_min_x", column.WorkMinX}, {"work_max_x", column.WorkMaxX},
            {"clip", rect(column.ClipRect)}, {"visible_x", column.IsVisibleX},
            {"visible_y", column.IsVisibleY}, {"skip_items", column.IsSkipItems}};
      }
    }
    return result;
  }
  void save_scroll_trace(const char *status) const {
    const auto encoded = Json{{"status", status}, {"attempts", scroll_trace}}.dump();
    check(encoded.size() <= 1024 * 1024, "Bounded scroll diagnostics exceed one MiB");
    std::ofstream stream(diagnostic_path, std::ios::binary | std::ios::trunc);
    check(stream.is_open(), "Cannot retain owned scroll diagnostics");
    stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    stream.close();
    check(!stream.fail(), "Writing owned scroll diagnostics failed");
  }
  Json control(const char *name) const {
    const std::string key = std::string("results.") + name;
    const auto item = last.at("host_controls").at(key);
    check(item == table().at("controls").at(name), "Results control projections disagree");
    return item;
  }
  void click(const char *name, bool enabled = true) {
    auto item = control(name);
    check(item.at("enabled").get<bool>() == enabled,
          std::string("Unexpected Results control admission: ") + name);
    for (int attempt = 0; attempt < 4; ++attempt) {
      frame(Json::array({{{"type", "position"}, {"space", "screen"}, {"point", item.at("point")}}}));
      auto next = control(name);
      if (next.at("id") == item.at("id") && next.at("point") == item.at("point")) {
        item = std::move(next);
        break;
      }
      check(attempt < 3, "Results control did not stabilize before its actual click");
      item = std::move(next);
    }
    const auto &context = *ImGui::GetCurrentContext();
    check(item.at("visible").get<bool>() && context.HoveredWindow &&
              std::string(context.HoveredWindow->Name) == item.at("window_name").get<std::string>() &&
              context.HoveredWindow->ClipRect.Contains(context.IO.MousePos),
          std::string("Results control does not own its actual hovered window and clip: ") + name);
    if (enabled)
      check(context.HoveredId == item.at("id").get<ImGuiID>(),
            std::string("Results click is not on the actual enabled ImGui item: ") + name);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame(); // Enabled flags reflect the newly selected page on this idle frame.
    ++clicks;
  }
  void show_column(std::size_t global_column) {
    try {
      // Four wheel units move at most ~104 pixels at the default font. The old
      // fixed twenty-unit motion could skip a field wider than the exposed
      // viewport and then stay at the wrong endpoint. Navigate by actual UI
      // geometry in either direction, never by assigning ImGui scroll state.
      for (int attempt = 0; attempt < 128; ++attempt) {
        check(scroll_trace.size() < 256, "Scroll observation exceeded its bounded attempt count");
        scroll_trace.push_back({{"column", global_column}, {"attempt", attempt},
                                {"before", scroll_state(global_column)}});
        auto &trace = scroll_trace.back();
        const Json *header = nullptr, *cell = nullptr, *anchor = nullptr;
        for (const auto &candidate : table().at("headers")) {
          if (candidate.at("column") == global_column) header = &candidate;
          if (!anchor && candidate.at("visible").get<bool>()) anchor = &candidate;
        }
        for (const auto &candidate : table().at("cells"))
          if (candidate.at("column") == global_column && candidate.at("row") == 0) cell = &candidate;
        check(header && cell, "Final particle field is absent from its bounded page");
        if (header->at("visible").get<bool>() && cell->at("visible").get<bool>()) {
          trace["visible"] = true;
          save_scroll_trace("observed");
          return;
        }
        check(anchor && attempt < 127, "Could not expose final field through actual horizontal scrolling");
        const auto anchor_rect = anchor->at("rect").get<std::array<float, 4>>();
        const auto cell_rect = cell->at("rect").get<std::array<float, 4>>();
        frame(Json::array({{{"type", "position"}, {"space", "screen"},
                            {"point", {(anchor_rect[0] + anchor_rect[2]) / 2,
                                       (anchor_rect[1] + anchor_rect[3]) / 2}}}}));
        trace["positioned"] = scroll_state(global_column);
        const auto *value = native_table();
        const auto *hovered = ImGui::GetCurrentContext()->HoveredWindow;
        check(value && value->InnerWindow && hovered == value->InnerWindow,
              "Horizontal wheel does not belong to the actual Results scrolling child");
        float left = value->InnerClipRect.Min.x;
        const int frozen = std::max<int>(value->FreezeColumnsCount, value->FreezeColumnsRequest);
        if (frozen > 0 && frozen <= value->ColumnsCount)
          left = std::max(left, value->Columns[frozen - 1].MaxX);
        const float right = value->InnerClipRect.Max.x;
        const float step = std::floor(std::min(2 * value->InnerWindow->FontRefSize,
                                               value->InnerWindow->InnerRect.GetWidth() * .67f));
        check(right > left && step > 0, "Results has no exposed horizontal scrolling viewport");
        const float maximum_units = std::min(4.f, (right - left) / (2 * step));
        const float distance = ((left + right) - (cell_rect[0] + cell_rect[2])) / 2;
        const float wheel = std::clamp(distance / step, -maximum_units, maximum_units);
        trace["wheel"] = {{"x", wheel}, {"y", 0}, {"step_pixels", step},
                          {"unfrozen_view", {left, right}}, {"pixel_displacement", -wheel * step}};
        frame(Json::array({{{"type", "wheel"}, {"x", wheel}, {"y", 0}}}));
        frame();
        trace["after"] = scroll_state(global_column);
      }
    } catch (...) {
      try { save_scroll_trace("failed"); }
      catch (const std::exception &error) { std::cerr << "Scroll diagnostic write failed: " << error.what() << '\n'; }
      std::cerr << "Owned scroll diagnostics: " << diagnostic_path.u8string() << '\n';
      throw;
    }
  }
};

void source_unchanged(const imagejxx::Document &owner, const DocumentSnapshot &before) {
  const auto after = owner.snapshot();
  const auto &a = after.display(), &b = before.display();
  check(after.same_document(before) && after.shares_data(before) &&
            after.data_identity() == before.data_identity() &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() && owner.undo_steps() == 0 && owner.redo_steps() == 0,
        "Results presentation changed source identity, carriers, revisions, or history");
  check(a.t == b.t && a.c == b.c && a.crosshair == b.crosshair && a.orthogonal == b.orthogonal &&
            a.linkedZoom == b.linkedZoom && a.zoom == b.zoom && a.planeZoom == b.planeZoom &&
            a.pan == b.pan && a.revision == b.revision && metadata_equal(a.properties, b.properties),
        "Results presentation changed source display state");
  const auto image = after.data().dataset.plane();
  check(image->width == 6 && image->height == 5 && image->channels == 1 && image->type == PixelType::UInt8 &&
            image->pixels.size() == source_words.size() &&
            std::memcmp(image->pixels.data(), source_words.data(), source_words.size() * sizeof(double)) == 0,
        "Results presentation changed literal source sample bytes or type");
}

void page(const Input &input, const std::vector<std::string> &names, std::size_t selected,
          bool has_row = true) {
  const auto &table = input.table();
  const auto first = selected * page_size;
  const auto end = std::min(names.size(), first + page_size);
  const auto pages = names.empty() ? 0 : 1 + (names.size() - 1) / page_size;
  check(table.at("visible").get<bool>() && table.at("page") == selected &&
            table.at("page_count") == pages && table.at("page_size") == page_size &&
            table.at("first_column") == first && table.at("end_column_exclusive") == end &&
            table.at("column_count") == names.size() && table.at("row_count") == (has_row ? 1 : 0),
        "Results page metadata differs from the complete ordered selection");
  check(table.at("begin_table").get<bool>() == !names.empty() &&
            !table.at("truncated").get<bool>() && table.at("headers").size() == end - first &&
            table.at("cells").size() == (has_row ? end - first : 0) && end - first <= page_size,
        "Results table was not bounded to the complete current 64-column page");
  for (std::size_t local = 0; local < end - first; ++local) {
    const auto global = first + local;
    const auto &header = table.at("headers").at(local);
    check(header.at("column") == global && header.at("name") == names[global] &&
              header.at("text") == names[global],
          "Header lost its global index, exact key, or literal emitted label");
    if (names[global] == "custom##literal")
      check(header.at("text_source") == "AddText.literal",
            "A literal ## name was interpreted as an ImGui label/ID separator");
    if (!has_row) continue;
    const auto &cell = table.at("cells").at(local);
    const auto found = std::find(particle_names.begin(), particle_names.end(), names[global]);
    const auto text = found == particle_names.end() ? std::string{} : particle_text[found - particle_names.begin()];
    check(cell.at("row") == 0 && cell.at("column") == global &&
              cell.at("name") == names[global] && cell.at("text") == text,
          "Rendered page cells differ from the independent literal particle values");
  }
  const auto &controls = table.at("controls");
  if (pages > 1) {
    check(controls.at("previous_columns").at("enabled").get<bool>() == (selected > 0) &&
              controls.at("next_columns").at("enabled").get<bool>() == (selected + 1 < pages),
          "Page endpoint controls permit an out-of-range page or block a valid page");
  } else {
    check(!controls.contains("previous_columns") && !controls.contains("next_columns"),
          "A small/empty table unexpectedly acquired pager controls");
  }
}

void multiline_header_layout(const Input &input) {
  const auto &header = input.table().at("headers").at(1);
  const auto &cell = input.table().at("cells").at(1);
  check(header.at("column") == 1 && header.at("name") == multiline_name &&
            header.at("text") == multiline_name && header.at("text_source") == "AddText.literal" &&
            header.at("visible").get<bool>() && cell.at("row") == 0,
        "Distinct multiline ## header was not literally emitted on the initial page");
  const auto text_size = ImGui::CalcTextSize(
      multiline_name.data(), multiline_name.data() + multiline_name.size(), false);
  const auto header_rect = header.at("rect").get<std::array<float, 4>>();
  const auto cell_rect = cell.at("rect").get<std::array<float, 4>>();
  const auto *table = input.native_table();
  check(table && table->ColumnsCount == static_cast<int>(page_size),
        "Multiline header has no actual current Results table");
  const auto padding = ImGui::GetStyle().CellPadding.y;
  const auto required_height = text_size.y + 2 * padding;
  const auto literal_bottom = header_rect[1] + padding + text_size.y;
  std::cout << "Multiline header: measured=" << text_size.y << ", padding=" << padding
            << ", header=[" << header_rect[1] << ',' << header_rect[3]
            << "], data_row_top=" << table->RowPosY1 << ", data_text_top=" << cell_rect[1]
            << ", literal_bottom=" << literal_bottom << '\n';
  check(text_size.y > ImGui::GetTextLineHeight(),
        "Multiline fixture did not measure more than the original one-line header");
  check(header_rect[3] - header_rect[1] >= required_height,
        "Literal multiline Results header did not reserve its full raw text height and padding");
  check(table->RowPosY1 >= header_rect[3] && table->RowPosY1 >= literal_bottom &&
            cell_rect[1] >= literal_bottom,
        "First Results data row overlaps the literal multiline header extent");
}

// Parse the written CSV independently, including quoted commas and escaped quotes.
std::vector<std::vector<std::string>> csv_rows(const fs::path &path) {
  const auto status = fs::symlink_status(path);
  check(fs::is_regular_file(status) && !fs::is_symlink(status) && fs::file_size(path) <= 64 * 1024,
        "Result CSV is missing, linked, or exceeds the bounded fixture output");
  std::ifstream stream(path, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool quoted = false, closed = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (quoted) {
      if (ch == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') { field += '"'; ++i; }
        else { quoted = false; closed = true; }
      } else field += ch;
    } else if (ch == '"') {
      check(field.empty() && !closed, "CSV quote begins within an unquoted/closed field");
      quoted = true;
    } else if (ch == ',' || ch == '\n') {
      row.push_back(std::move(field)); field.clear(); closed = false;
      check(row.size() <= column_count, "CSV exceeded the selected-column bound");
      if (ch == '\n') {
        rows.push_back(std::move(row)); row.clear();
        check(rows.size() <= 2, "CSV contains more than the header and actual one-row result");
      }
    } else {
      check(!closed && ch != '\r', "CSV has trailing data after a quoted field or a noncanonical newline");
      field += ch;
    }
  }
  check(!quoted && !closed && field.empty() && row.empty(), "CSV is not newline-terminated or is incomplete");
  return rows;
}

void run(const fs::path &directory) {
  const auto names = wide_names();
  Settings admitted;
  admitted.set("measurements", names);
  check(admitted.strings("measurements") == names && names.size() == column_count,
        "Public Settings did not admit the exact supported 1,024-name boundary");

  Image image(6, 5, 1, PixelType::UInt8);
  image.pixels.assign(source_words.begin(), source_words.end());
  DocumentData data{Dataset::from_planes({1, 1, 1, 5, 6, 1}, Stack{std::move(image)})};
  data.metadata.title = "CFG3 wide measurement columns";
  data.metadata.properties["cfg3"] = "preserve metadata";
  data.metadata.extensions["cfg3"] = "preserve extension";
  data.metadata.record_layout["cfg3"] = "preserve layout";
  data.previous_selection = Selection("line", {{0, 4}, {5, 0}});
  data.rois = {{"preserved ROI", Selection("rectangle", {{0, 0}, {1, 1}}), {}, {}, {}, {}}};
  auto owner = std::make_shared<imagejxx::Document>(std::move(data));
  auto display = owner->snapshot().display();
  display.zoom = 32; display.crosshair = {2, 2, 0};
  display.properties["display_range"] = metadata_array({0., 255.});
  owner->set_display(display);
  Window window((directory / "preferences.json").u8string());
  window.addNativeDocument(owner);
  window.updateSettings({{"measurements", names}}, false);
  Input input(window, directory);
  const auto before = owner->snapshot();
  auto expected_settings = window.settings();
  const auto workflow = window.workflow;
  const auto assert_state = [&](const Json &raw) {
    source_unchanged(*owner, before);
    check(window.settings().values() == expected_settings.values() && window.results == raw &&
              window.workflow == workflow && !window.busy() && window.jobs().empty(),
          "Results action changed full settings, raw results, workflow, or processing jobs");
    check(!fs::exists(directory / "preferences.json"), "Nonpersistent page selection wrote Preferences");
  };

  std::cout << "RUN CFG3-R01-real-particle-boundary\n";
  check(window.results.empty(), "Test must not seed synthetic raw results");
  window.action("particles", {{"connectivity", 8}, {"min_area", 0}, {"max_area", nullptr}});
  input.frame(); input.frame();
  const auto raw = window.results;
  check(raw.size() == 1 && raw[0].at("min") == 2. && raw[0].at("max") == 7. &&
            raw[0].at("bbox") == Json::array({1, 1, 3, 2}) && raw[0].at("pixels") == 6 &&
            raw[0].at("label") == 1 && raw[0].at("area") == 6. &&
            raw[0].at("mean") == 4.5 && raw[0].at("integrated_density") == 27.,
        "Actual particles action differs from the literal six-pixel component");
  assert_state(raw); page(input, names, 0);
  check(input.table().at("headers").at(0).at("visible").get<bool>(),
        "Literal ## header was not visibly emitted on the initial page");
  multiline_header_layout(input);
  std::cout << "PASS CFG3-R01-real-particle-boundary\n";

  std::cout << "RUN CFG3-R02-actual-pages-and-endpoints\n";
  input.click("previous_columns", false); page(input, names, 0); assert_state(raw);
  for (std::size_t current = 1; current < 16; ++current) {
    input.click("next_columns"); page(input, names, current); assert_state(raw);
  }
  for (std::size_t current = custom_count; current < column_count; ++current) {
    input.show_column(current); page(input, names, 15); assert_state(raw);
  }
  input.click("next_columns", false); page(input, names, 15); assert_state(raw);
  for (std::size_t current = 15; current > 0; --current) {
    input.click("previous_columns"); page(input, names, current - 1); assert_state(raw);
  }
  std::cout << "PASS CFG3-R02-actual-pages-and-endpoints\n";

  std::cout << "RUN CFG3-R03-full-selected-csv\n";
  const auto csv = directory / "wide-results.csv";
  // This is the exposed action, not a claim of file-browser interaction.
  window.action("save_results", {{"path", csv.u8string()}});
  const auto rows = csv_rows(csv);
  check(rows.size() == 2 && rows[0] == names && rows[1].size() == column_count,
        "CSV exported a page instead of all 1,024 selected columns in order");
  for (std::size_t i = 0; i < custom_count; ++i)
    check(rows[1][i].empty(), "Absent custom column acquired a value in CSV");
  for (std::size_t i = 0; i < particle_text.size(); ++i)
    check(rows[1][custom_count + i] == particle_text[i], "CSV lost a literal final particle value");
  input.frame(); page(input, names, 0); assert_state(raw);
  std::cout << "PASS CFG3-R03-full-selected-csv\n";

  std::cout << "RUN CFG3-R04-reset-small-and-clear\n";
  input.click("next_columns"); input.click("next_columns"); page(input, names, 2);
  auto reordered = names;
  std::swap(reordered[0], reordered[1]);
  window.updateSettings({{"measurements", reordered}}, false);
  expected_settings.set("measurements", reordered);
  input.frame(); input.frame(); page(input, reordered, 0); assert_state(raw);
  input.click("next_columns"); page(input, reordered, 1);
  window.updateSettings({{"measurements", particle_names}}, false);
  expected_settings.set("measurements", particle_names);
  input.frame(); input.frame(); page(input, particle_names, 0); assert_state(raw);
  window.updateSettings({{"measurements", names}}, false);
  expected_settings.set("measurements", names);
  input.frame(); input.frame(); page(input, names, 0);
  input.click("next_columns"); input.click("next_columns"); page(input, names, 2);
  input.click("clear"); page(input, names, 0, false); assert_state(Json::array());
  window.updateSettings({{"measurements", Json::array()}}, false);
  expected_settings.set("measurements", std::vector<std::string>{});
  input.frame(); input.frame(); page(input, {}, 0, false); assert_state(Json::array());
  std::cout << "PASS CFG3-R04-reset-small-and-clear\n";

  input.session.close();
  check(!ImGui::GetCurrentContext(), "Results test did not release its actual ImGui context");
  source_unchanged(*owner, before);
  std::cout << "Results measurement columns: 4 groups, " << checks << " checks, "
            << input.frames << " actual frames, " << input.clicks << " actual click pairs\n";
  std::cout << "Owned CSV retained for C: " << csv.u8string() << ", bytes=" << fs::file_size(csv) << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply one C-owned FRESH_DIRECTORY for the wide Results consumer");
    const auto directory = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(directory != directory.root_path(), "Scratch must not be a filesystem root");
    std::error_code error;
    const auto status = fs::symlink_status(directory, error);
    check(status.type() == fs::file_type::not_found, "Results scratch must be absent, including links");
    const auto parent = fs::symlink_status(directory.parent_path());
    check(fs::is_directory(parent) && !fs::is_symlink(parent), "C must supply an existing regular scratch parent");
    check(fs::create_directory(directory), "Cannot create owned Results scratch");
    run(directory);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CFG3 Results failure; retain owned scratch: " << error.what() << '\n';
    return 1;
  }
}
