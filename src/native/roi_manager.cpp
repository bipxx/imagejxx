#include <imagejxx/roi_manager.hpp>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/planar_annotations.hpp>
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
PreparedRoiManagerEdit select_value(const DocumentSnapshot &source,const Roi &value,
                                    std::optional<std::size_t> index) {
  PreparedRoiManagerEdit result{source.begin_edit(Change::All),source.display(),index};
  auto named=value.selection;named.attributes["imagej_roi_name"]=value.name;
  if(!source.data().selection || *source.data().selection!=named)
    result.transaction.select(std::move(named));
  if(value.t)result.display->t=*value.t;
  if(value.c)result.display->c=*value.c;
  if(value.z)result.display->crosshair[2]=*value.z;
  result.display->validate(source.data().dataset.shape());
  return result;
}
Roi annotation_roi(const DocumentSnapshot &source,std::size_t index,std::size_t cap) {
  auto rows=planar_annotations(source.data(),cap);
  if(index>=rows.size())throw std::out_of_range("Select a native annotation");
  auto &value=rows[index];
  if(value.plane!=Plane::XY)
    throw std::invalid_argument("Only XY annotations can be copied without projection");
  return {std::move(value.name),std::move(value.selection),value.t,value.c,value.coordinate,std::move(value.attributes)};
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
  auto selection = selected(source);
  // Selection projects the row name without editing the stored geometry. Do
  // not persist that projection as a metadata/history change on Update.
  const auto name = selection.attributes.find("imagej_roi_name");
  if (name != selection.attributes.end()) {
    const auto text = std::get_if<std::string>(&name->second);
    if (text && *text == value.name) {
      const auto original = value.selection.attributes.find("imagej_roi_name");
      if (original == value.selection.attributes.end())
        selection.attributes.erase(name);
      else
        name->second = original->second;
    }
  }
  value.selection = std::move(selection);
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
  return select_value(source,row(source,index),index);
}
PreparedRoiManagerEdit prepare_select_planar_annotation(const DocumentSnapshot &source,
    std::size_t index,std::size_t cap) {
  const auto value=annotation_roi(source,index,cap);
  return select_value(source,value,{});
}
PreparedRoiManagerEdit prepare_store_planar_annotation_roi(const DocumentSnapshot &source,
    std::size_t index,std::size_t cap) {
  auto value=annotation_roi(source,index,cap);
  const auto target=source.data().rois.size();
  auto rows=source.data().rois;rows.push_back(std::move(value));
  PreparedRoiManagerEdit result{source.begin_edit(Change::All),{},target};
  result.transaction.set_rois(std::move(rows));
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
