#include <imagejxx/roi_manager.hpp>
#include <imagejxx/roi_style.hpp>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {
const Roi &row(const DocumentSnapshot &source, std::size_t index) {
  if (index >= source.data().rois.size())
    throw std::out_of_range("Select a saved ROI");
  return source.data().rois[index];
}
const Selection &selected(const DocumentSnapshot &source) {
  if (!source.data().selection)
    throw std::invalid_argument("Draw a selection first");
  return *source.data().selection;
}
PreparedRoiManagerEdit replace(const DocumentSnapshot &source, std::size_t index,
                               Roi value) {
  const auto &original = row(source, index);
  value.validate(source.data().dataset.shape());
  PreparedRoiManagerEdit result{source.begin_edit(Change::All), {}, index};
  // Exact no-ops preserve Previous, redo, dirty state and every revision.
  if (!(value == original)) {
    auto rows = source.data().rois;
    rows[index] = std::move(value);
    result.transaction.set_rois(std::move(rows));
  }
  return result;
}
} // namespace

PreparedRoiManagerEdit prepare_store_roi(const DocumentSnapshot &source,
                                        std::string name, bool all_slices) {
  const auto &selection = selected(source);
  const auto &view = source.display();
  const auto index = source.data().rois.size();
  if (name.empty()) name = "ROI " + std::to_string(index + 1);
  Roi value{std::move(name), selection, view.t, view.c,
            all_slices ? std::optional<int>{} : std::optional<int>{view.crosshair[2]}, {}};
  value.validate(source.data().dataset.shape());
  auto rows = source.data().rois;
  rows.push_back(std::move(value));
  PreparedRoiManagerEdit result{source.begin_edit(Change::All), {}, index};
  result.transaction.set_rois(std::move(rows));
  return result;
}
PreparedRoiManagerEdit prepare_update_roi(const DocumentSnapshot &source,
                                         std::size_t index) {
  auto value = row(source, index);
  value.selection = selected(source);
  return replace(source, index, std::move(value));
}
PreparedRoiManagerEdit prepare_rename_roi(const DocumentSnapshot &source,
                                         std::size_t index, std::string name) {
  auto value = row(source, index);
  value.name = std::move(name);
  return replace(source, index, std::move(value));
}
PreparedRoiManagerEdit prepare_delete_roi(const DocumentSnapshot &source,
                                         std::size_t index) {
  (void)row(source, index);
  auto rows = source.data().rois;
  rows.erase(rows.begin() + index);
  PreparedRoiManagerEdit result{source.begin_edit(Change::All), {}, {}};
  result.transaction.set_rois(std::move(rows));
  return result;
}
PreparedRoiManagerEdit prepare_select_roi(const DocumentSnapshot &source,
                                         std::size_t index) {
  const auto &value = row(source, index);
  PreparedRoiManagerEdit result{source.begin_edit(Change::All), source.display(), index};
  if (!source.data().selection || *source.data().selection != value.selection)
    result.transaction.select(value.selection);
  // optional presence, rather than truthiness of the stored integer, preserves
  // explicit index zero. Absent axes retain the current position.
  if (value.t) result.display->t = *value.t;
  if (value.c) result.display->c = *value.c;
  if (value.z) result.display->crosshair[2] = *value.z;
  result.display->validate(source.data().dataset.shape());
  return result;
}
PreparedRoiManagerEdit prepare_roi_properties(const DocumentSnapshot &source,
    std::size_t index, std::string name, const MetadataObject &style_patch,
    std::optional<int> t, std::optional<int> c, std::optional<int> z) {
  auto value = row(source, index);
  value.name = std::move(name);
  value.selection = with_roi_style(value.selection, style_patch);
  value.t = t; value.c = c; value.z = z;
  return replace(source, index, std::move(value));
}
} // namespace imagejxx
