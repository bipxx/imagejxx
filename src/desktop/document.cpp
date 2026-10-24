#include "desktop.hpp"
#include "../native/selection_codec.hpp"
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/imagej_annotation_import.hpp>
#include <imagejxx/indexed_palette.hpp>
#include "../native/retained_resource_internal.hpp"
#include "../native/session_metadata.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>
#include <imagejxx/axis_extraction_command.hpp>
#include <imagejxx/axis_orientation_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/targets.hpp>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <zlib.h>
namespace imagejxx::desktop {
namespace session_metadata = imagejxx::detail::session_metadata;
namespace {
bool calibrationField(const std::string &key) {
  return session_metadata::calibration_field(key);
}
bool documentField(const std::string &key) {
  return session_metadata::document_field(key);
}
MetadataObject attributes(const Json &value,
                          std::initializer_list<const char *> fields) {
  if (!value.is_object())
    throw std::invalid_argument("Document record must be an object");
  MetadataObject result;
  for (auto it = value.begin(); it != value.end(); ++it)
    if (std::none_of(fields.begin(), fields.end(),
                     [&](const char *field) { return it.key() == field; }))
      result[it.key()] = metadataFromJson(it.value());
  return result;
}
bool sameOverlays(const std::vector<MetadataObject> &a,
                  const std::vector<MetadataObject> &b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!metadata_equal(a[i], b[i]))
      return false;
  return true;
}
Image canonicalSource(Image image, FloatingSamplePolicy policy) {
  if (policy == FloatingSamplePolicy::FiniteOnly)
    return canonical(std::move(image));
  if (policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
      policy == FloatingSamplePolicy::PreserveIEEEFloat32)
    return canonical_float32_source(std::move(image), policy);
  throw std::invalid_argument("Invalid floating sample policy");
}
SharedPlanes canonicalPlanes(Stack frames, FloatingSamplePolicy policy) {
  for (auto &frame : frames)
    frame = canonicalSource(std::move(frame), policy);
  return SharedPlanes(std::move(frames), policy);
}
bool isCanonical(const Image &image, FloatingSamplePolicy policy) {
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid floating sample policy");
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      (image.type != PixelType::Float32 || image.channels != 1))
    return false;
  for (const double &value : image.pixels) {
    if (image.type == PixelType::UInt8 || image.type == PixelType::UInt16) {
      if (value !=
          std::floor(std::clamp(value, 0.0, type_max(image.type)) + .5))
        return false;
    } else if (image.type == PixelType::Float32) {
      try {
        const auto bits = float32_source_sample_bits(value, policy);
        const auto represented = float32_source_sample(bits, policy);
        // Compare sample representation, never NaN value equality. The typed
        // helper enforces the selected source policy and exact representation.
        if (std::memcmp(&value, &represented, sizeof(value)) != 0)
          return false;
      } catch (const std::invalid_argument &) {
        return false;
      }
    }
  }
  return true;
}
FloatingSamplePolicy sessionSamplePolicy(const Json &item) {
  if (!item.contains("pixel_sample_policy"))
    return FloatingSamplePolicy::FiniteOnly;
  const auto &policy = item.at("pixel_sample_policy");
  if (policy.is_string()) {
    const auto token=policy.get<std::string>();
    if(token=="preserve_quiet_nan_float32")return FloatingSamplePolicy::PreserveQuietNaNFloat32;
    if(token=="preserve_ieee_float32")return FloatingSamplePolicy::PreserveIEEEFloat32;
  }
  throw std::invalid_argument("Invalid session pixel sample policy");
}
void validateNumericJson(const Json &root) {
  std::vector<const Json *> pending{&root};
  while (!pending.empty()) {
    const auto *value = pending.back();
    pending.pop_back();
    if (value->is_number_float())
      require_finite(value->get<double>(), "Session JSON number");
    else if (value->is_array() || value->is_object())
      for (const auto &child : *value)
        pending.push_back(&child);
  }
}
std::vector<std::array<double, 2>> points(const Json &selection) {
  std::vector<std::array<double, 2>> result;
  const auto &values = get(selection, "points");
  if (!values.is_null() && !values.is_array())
    throw std::invalid_argument("Invalid selection points");
  for (const auto &v : values) {
    if (!v.is_array() || v.size() != 2)
      throw std::invalid_argument("Invalid selection point");
    result.push_back({number(v[0]), number(v[1])});
  }
  return result;
}
std::vector<unsigned char> rawDoubles(
    const std::vector<double> &values,
    FloatingSamplePolicy policy = FloatingSamplePolicy::FiniteOnly) {
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid floating sample policy");
  if (values.size() > 1024ULL * 1024 * 1024 / 8)
    throw std::invalid_argument("Image exceeds session limit");
  std::vector<unsigned char> data(values.size() * 8);
  for (size_t i = 0; i < values.size(); ++i) {
    if (policy == FloatingSamplePolicy::FiniteOnly)
      require_finite(values[i], "Session pixel");
    else
      (void)float32_source_sample_bits(values[i], policy);
    uint64_t bits;
    std::memcpy(&bits, &values[i], 8);
    for (int k = 0; k < 8; ++k)
      data[i * 8 + k] = static_cast<unsigned char>(bits >> (8 * k));
  }
  return data;
}
std::vector<double> decodeDoubles(
    const Json &value, size_t count,
    FloatingSamplePolicy policy = FloatingSamplePolicy::FiniteOnly) {
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid floating sample policy");
  if (count > 1024ULL * 1024 * 1024 / 8)
    throw std::invalid_argument("Session exceeds size limit");
  auto data = unbase64(string(value));
  if (data.size() != count * 8)
    throw std::invalid_argument("Invalid session pixel payload");
  std::vector<double> result(count);
  for (size_t i = 0; i < count; ++i) {
    uint64_t bits = 0;
    for (int k = 0; k < 8; ++k)
      bits |= uint64_t(data[i * 8 + k]) << (8 * k);
    std::memcpy(&result[i], &bits, 8);
    if (policy == FloatingSamplePolicy::FiniteOnly)
      require_finite(result[i], "Session pixel");
    else
      (void)float32_source_sample_bits(result[i], policy);
  }
  return result;
}
} // namespace
Json maskSelection(const Image &mask) {
  require_gray(mask);
  std::vector<unsigned char> data((mask.pixels.size() + 7) / 8, 0);
  for (size_t i = 0; i < mask.pixels.size(); ++i)
    if (mask.pixels[i])
      data[i / 8] |= 1 << (7 - i % 8);
  return {{"kind", "mask"},
          {"points", Json::array()},
          {"width", 1},
          {"shape", {mask.height, mask.width}},
          {"mask", base64(data)}};
}
Image selectionMask(const Json &selection, int w, int h) {
  return selectionFromJson(selection).mask(w, h);
}
Selection selectionFromJson(const Json &value) {
  if(get(value,"kind")=="path")return imagejxx::detail::selection_codec::decode_selection(value,SessionLimits{},true,true);
  return session_metadata::selection_from_json(value);
}
Json selectionJson(const Selection &value) {
  if(value.is_path())return imagejxx::detail::selection_codec::encode_selection(value,true,true);
  return session_metadata::selection_to_json(value);
}
void preserveDisplay(Json &destination, const Json &source) {
  for (auto key : {"display_range", "lut", "custom_lut", manual_threshold_property, "channel_display"}) {
    if (source.contains(key))
      destination[key] = source[key];
    else
      destination.erase(key);
  }
}
Change stateChanges(const State &before, const State &after) {
  Change changed = Change::None;
  if (before.frames.size() != after.frames.size() ||
      before.frames.sample_policy() != after.frames.sample_policy())
    changed = Change::Pixels;
  else
    for (std::size_t k = 0; k < before.frames.size(); ++k)
      if (before.frames.shared(k) != after.frames.shared(k)) {
        changed = Change::Pixels;
        break;
      }
  auto a = before.metadata, b = after.metadata;
  if (get(a, "selection") != get(b, "selection") ||
      get(a, "rois") != get(b, "rois"))
    changed = changed | Change::Selection;
  for (auto key : {"display_range", "lut", "custom_lut", manual_threshold_property, "channel_display"}) {
    if (get(a, key) != get(b, key))
      changed = changed | Change::Display;
    a.erase(key);
    b.erase(key);
  }
  for (auto key : {"slice_index", "selection", "previous_selection"}) {
    a.erase(key);
    b.erase(key);
  }
  if (a != b)
    changed = changed | Change::Metadata;
  if (before.spectrum != after.spectrum)
    changed = changed | Change::Metadata;
  return changed;
}
Rect selectionBounds(const Json &s, int w, int h) {
  auto mask = selectionMask(s, w, h);
  int x1 = w, y1 = h, x2 = -1, y2 = -1;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (mask.at(x, y) != 0) {
        x1 = std::min(x1, x);
        y1 = std::min(y1, y);
        x2 = std::max(x2, x);
        y2 = std::max(y2, y);
      }
  if (x2 < 0)
    throw std::invalid_argument("Selection contains no pixels");
  return {x1, y1, x2 - x1 + 1, y2 - y1 + 1};
}

namespace {
bool samePlanes(const SharedPlanes &a, const SharedPlanes &b) {
  if (a.size() != b.size() || a.sample_policy() != b.sample_policy())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a.shared(i) != b.shared(i))
      return false;
  return true;
}
State initialState(SharedPlanes frames, std::string title) {
  State state;
  state.frames = std::move(frames);
  state.metadata = session_metadata::initial_metadata(state.frames.size(), title);
  return state;
}
Json nativeMetadata(const DocumentData &data);
bool scientificData(const DocumentData &data) {
  const auto shape = data.dataset.shape();
  return !data.dataset.has_eager_planes() || shape.t > 1 || shape.c > 1 ||
         std::any_of(data.rois.begin(), data.rois.end(),
                     [](const Roi &roi) { return roi.t || roi.c; });
}
SharedPlanes selectedPlanes(const SharedPlanes &planes, DatasetShape shape,
                            const DisplayState &display) {
  if (shape.t == 1 && shape.c == 1)
    return planes;
  std::vector<std::size_t> indices;
  indices.reserve(std::size_t(shape.z));
  const auto first = (std::size_t(display.t) * std::size_t(shape.c) +
                      std::size_t(display.c)) * std::size_t(shape.z);
  for (int z = 0; z < shape.z; ++z)
    indices.push_back(first + std::size_t(z));
  return planes.subset(indices);
}
DocumentData stateData(const State &state,
                       const DocumentSnapshot *source = nullptr) {
  if ((source && scientificData(source->data())) ||
      (state.source && scientificData(state.source->data())))
    throw std::invalid_argument(
        "Scientific desktop documents are read-only until scientific editing is supported");
  // Wire policy is reserved and derived from immutable source ownership; an
  // ordinary metadata extension cannot turn on nonfinite sample admission.
  if (state.metadata.contains("pixel_sample_policy"))
    throw std::invalid_argument("Reserved desktop field: pixel_sample_policy");
  if (state.frames.empty())
    throw std::invalid_argument("Empty document");
  const auto &f = state.frames[0];
  auto cal = calibrationFromJson(get(state.metadata, "calibration"));
  auto prior = source ? source->data().dataset.shared_planes()
                      : std::optional<SharedPlanes>{};
  auto dataset =
      prior && samePlanes(*prior, state.frames)
          ? source->data().dataset.with_calibration(cal)
          : Dataset::from_shared_planes(
                {1, 1, int(state.frames.size()), f.height, f.width, f.channels},
                state.frames, cal);
  DocumentData data{std::move(dataset),
                    {string(get(state.metadata, "title"), "Untitled"), {}},
                    {},
                    {}};
  const auto priorMetadata =
      source ? nativeMetadata(source->data()) : Json::object();
  const auto &selected = get(state.metadata, "selection");
  if (!selected.empty())
    data.selection = selectionFromJson(selected);
  const auto &previous = get(state.metadata, "previous_selection");
  if (!previous.empty())
    data.previous_selection = selectionFromJson(previous);
  const auto &rois = get(state.metadata, "rois");
  if (!rois.is_null() && !rois.is_array())
    throw std::invalid_argument("Invalid session ROIs");
  for (const auto &roi : rois) {
    Roi value{string(get(roi, "name")),
              selectionFromJson(get(roi, "selection")),
              {},
              {},
              {}};
    if (!get(roi, "slice_index").is_null())
      value.z = integer(get(roi, "slice_index"));
    value.attributes = attributes(roi, {"name", "selection", "slice_index"});
    data.rois.push_back(std::move(value));
  }
  data.metadata = session_metadata::infer_metadata(
      state.metadata, source ? &source->data().metadata : nullptr,
      source ? &priorMetadata : nullptr);
  const auto &overlays = get(state.metadata, "overlays");
  if (!overlays.is_null() && !overlays.is_array())
    throw std::invalid_argument("Invalid overlays");
  for (const auto &overlay : overlays)
    data.overlays.push_back(metadataObjectFromJson(overlay));
  data.spectrum = state.spectrum;
  data.validate();
  return data;
}
DisplayState stateDisplay(const State &state, DisplayState display,
                          DatasetShape shape) {
  display.properties = session_metadata::display_properties(state.metadata);
  display.crosshair[2] = integer(get(state.metadata, "slice_index"));
  display.normalize(shape);
  return display;
}
void validateDesktop(const DocumentData &data, const DisplayState &display,
                     bool eagerAdmitted = false, bool allowVirtual=false,
                     bool allowPlanarPaths=false) {
  const auto path=[](const std::optional<Selection> &value){return value && value->is_path();};
  if(!allowPlanarPaths && (path(data.selection) || path(data.previous_selection) ||
      std::any_of(data.rois.begin(),data.rois.end(),[](const auto &row){return row.selection.is_path();})))
    throw std::invalid_argument("Desktop path selections require versioned path support");
  if(!allowPlanarPaths)
  if(const auto found=data.metadata.extensions.find(planar_annotations_extension);found!=data.metadata.extensions.end())
    if(const auto node=std::get_if<std::shared_ptr<const MetadataTree>>(&found->second);node && *node)
      if(const auto object=std::get_if<MetadataObject>(&(*node)->value))
        if(const auto version=object->find("version");version!=object->end()) {
          const auto signed_version=std::get_if<std::int64_t>(&version->second);
          const auto unsigned_version=std::get_if<std::uint64_t>(&version->second);
          if((signed_version && *signed_version==3) || (unsigned_version && *unsigned_version==3))
            throw std::invalid_argument("Desktop planar path annotations require versioned path support");
        }
  const auto shape = data.dataset.shape();
  manual_threshold_state(display, shape);
  recognized_channel_display(display, data.dataset);
  if (!allowVirtual && !eagerAdmitted && !data.dataset.shared_planes())
    throw std::invalid_argument(
        "Desktop virtual sources are not supported yet");
  if ((!data.dataset.has_eager_planes() || shape.t > 1 || shape.c > 1) && data.spectrum)
    throw std::invalid_argument(
        "Desktop scientific Fourier positions are not supported yet");
  for (const auto &[key, value] : data.metadata.extensions) {
    if (key == "calibration") {
      auto fields = metadataJson(value);
      if (!fields.is_object())
        throw std::invalid_argument("Calibration extensions must be an object");
      for (auto it = fields.begin(); it != fields.end(); ++it)
        if (calibrationField(it.key()))
          throw std::invalid_argument(
              "Use set_calibration for spatial/time fields");
    } else if (documentField(key))
      throw std::invalid_argument("Reserved desktop extension field: " + key);
  }
  const auto layout = metadataJson(data.metadata.record_layout);
  for (auto key : {"root_fields", "null_fields", "calibration_fields",
                   "roi_slice_fields"}) {
    const auto &fields = get(layout, key);
    if (!fields.is_null() && !fields.is_array())
      throw std::invalid_argument("Invalid legacy record layout");
    for (const auto &field : fields)
      if (!field.is_string())
        throw std::invalid_argument("Invalid legacy record field");
  }
  auto fields = metadataJson(display.properties);
  for (auto it = fields.begin(); it != fields.end(); ++it)
    if (it.key() != "display_range" && it.key() != "lut" &&
        it.key() != "custom_lut" && it.key() != manual_threshold_property &&
        it.key() != "channel_display")
      throw std::invalid_argument("Unsupported desktop display property: " +
                                  it.key());
  const auto &range = get(fields, "display_range");
  if (!range.is_null()) {
    if (!range.is_array() || range.size() != 2)
      throw std::invalid_argument("Display range needs two ordered endpoints");
    const double low=number(range[0]),high=number(range[1]);
    const bool typedEqual=shape.s==1 && range[0].is_number() && range[1].is_number() &&
        (data.dataset.pixel_type()==PixelType::UInt16 || data.dataset.pixel_type()==PixelType::Float32);
    if (low>high || (low==high && !typedEqual))
      throw std::invalid_argument("Display range needs ordered endpoints for its scalar type");
  }
  if (!get(fields, "lut").is_null() && !get(fields, "lut").is_string())
    throw std::invalid_argument("Display LUT must be a name");
  const auto &lut = get(fields, "custom_lut");
  if (!lut.is_null()) {
    if (!lut.is_array() || lut.size() != 256)
      throw std::invalid_argument("LUT needs 256 RGB rows");
    for (const auto &row : lut) {
      if (!row.is_array() || row.size() != 3)
        throw std::invalid_argument("Invalid custom LUT");
      for (const auto &entry : row)
        if (number(entry) < 0 || number(entry) > 255)
          throw std::invalid_argument("LUT colors must be 0..255");
    }
  }
}
std::shared_ptr<imagejxx::Document> makeOwner(State state) {
  if (state.source && scientificData(state.source->data()))
    throw std::invalid_argument(
        "Scientific desktop documents are read-only until scientific editing is supported");
  if (state.frames.empty())
    throw std::invalid_argument("Empty document");
  auto defaults = initialState(state.frames, "Untitled").metadata;
  defaults.update(state.metadata);
  state.metadata = std::move(defaults);
  for (std::size_t i = 0; i < state.frames.size(); ++i) {
    require_same_shape(state.frames[0], state.frames[i]);
    if (state.frames[i].type != state.frames[0].type)
      throw std::invalid_argument("Mixed stack types");
    if (!isCanonical(state.frames[i], state.frames.sample_policy()))
      state.frames.replace(
          i, canonicalSource(state.frames[i], state.frames.sample_policy()));
  }
  auto data = stateData(state);
  DisplayState display;
  display.crosshair = {data.dataset.shape().x / 2, data.dataset.shape().y / 2,
                       0};
  display = stateDisplay(state, display, data.dataset.shape());
  validateDesktop(data, display);
  return std::make_shared<imagejxx::Document>(std::move(data), display);
}
Json nativeMetadata(const DocumentData &data) {
  session_metadata::ScalarRecord scalar;
  scalar.metadata = data.metadata;
  scalar.calibration = data.dataset.calibration();
  scalar.plane_count = data.dataset.shape().z;
  scalar.selection = data.selection;
  scalar.previous_selection = data.previous_selection;
  scalar.rois = data.rois;
  scalar.overlays = data.overlays;
  return session_metadata::wire_metadata(scalar,true);
}
} // namespace
Document::Document(Stack frames, std::string title, Json metadata,
                   FloatingSamplePolicy sample_policy)
    : Document(canonicalPlanes(std::move(frames), sample_policy), std::move(title),
               std::move(metadata)) {}
Document::Document(SharedPlanes frames, std::string title, Json metadata)
    : Document([&] {
        auto initial = initialState(std::move(frames), std::move(title));
        initial.metadata.update(metadata);
        return initial;
      }()) {}
Document::Document(State initial) : Document(makeOwner(std::move(initial)),false,128ULL*1024*1024,false) {}
struct Document::PathAdmission {
  std::thread::id thread;
  bool enabled;
};
Document::Document(std::shared_ptr<imagejxx::Document> document, bool allowVirtual, std::size_t maxViewBytes, bool allowPlanarPaths)
    : id(document ? document->id
                  : throw std::invalid_argument("Document owner is required")),
      native_(std::move(document)), maxViewBytes_(maxViewBytes) {
  if(!maxViewBytes_)throw std::invalid_argument("Virtual view byte limit must be positive");
  const auto thread = std::this_thread::get_id();
  pathAdmission_=std::make_shared<PathAdmission>(PathAdmission{thread,allowPlanarPaths});
  validation_ = native_->constrain(
      [thread, allowVirtual, paths=pathAdmission_, admitted = std::optional<Dataset>{}](
          const DocumentData &data, const DisplayState &display) mutable {
        if (std::this_thread::get_id() != thread)
          throw std::runtime_error(
              "Desktop documents must be edited on their creator thread");
        // Eager-source discovery copies the immutable plane table. Reuse the
        // admission for the same source so camera and T/C navigation do not
        // repeatedly copy T*C*Z shared pointers. No image samples are read.
        const bool sameSource =
            admitted && admitted->shares_pixels(data.dataset);
        validateDesktop(data, display, sameSource, allowVirtual, paths->enabled);
        // Admission caches pixel identity, not a running/READY job lease.
        // Staged publication can detach that lease without changing pixels.
        if (!sameSource)
          admitted = imagejxx::detail::ResourceAccess::detach(data.dataset);
      });
}
Document::Document(const DocumentSnapshot &source, WorkerTag)
    : Document(std::make_shared<imagejxx::Document>(source),!source.data().dataset.has_eager_planes()) {
  validation_.reset();
  origin_ = source;
}
void Document::refresh() const {
  auto current = nativeSnapshot();
  if (cached_ && current.shares_data(*cached_) &&
      current.revisions().matches(cached_->revisions(), Change::All) &&
      current.data_identity() == cached_->data_identity() &&
      current.dirty() == cached_->dirty())
    return;
  const bool dataChanged = !cached_ || !current.shares_data(*cached_);
  const bool axesChanged = !cached_ ||
                          current.display().t != cached_->display().t ||
                          current.display().c != cached_->display().c;
  if (dataChanged) {
    auto planes = current.data().dataset.shared_planes();
    auto frames = planes ? selectedPlanes(*planes, current.data().dataset.shape(),
                                 current.display()) : SharedPlanes{};
    auto metadata = nativeMetadata(current.data());
    cachedPlanes_ = std::move(planes);
    cachedState_.frames = std::move(frames);
    cachedState_.metadata = std::move(metadata);
    cachedState_.spectrum = current.data().spectrum;
  } else if (axesChanged && cachedPlanes_)
    cachedState_.frames = selectedPlanes(*cachedPlanes_,
                                        current.data().dataset.shape(),
                                        current.display());
  auto &meta = cachedState_.metadata;
  if (!cached_ || !current.shares_data(*cached_) ||
      current.display().revision != cached_->display().revision)
    preserveDisplay(meta, metadataJson(current.display().properties));
  meta["slice_index"] = current.display().crosshair[2];
  cachedState_.identity = current.data_identity().value_or(0);
  cachedState_.source =
      origin_ ? origin_ : std::optional<DocumentSnapshot>(current);
  cached_ = std::move(current);
}
const State &Document::state() const {
  refresh();
  return cachedState_;
}
DatasetShape Document::shape() const {
  return nativeSnapshot().data().dataset.shape();
}
PlanePosition Document::position() const {
  const auto snapshot = nativeSnapshot();
  const auto &display = snapshot.display();
  return {display.t, display.c, display.crosshair[2]};
}
bool Document::scientific() const {
  return scientificData(nativeSnapshot().data());
}
void Document::requireLegacyEditable() const {
  if (scientific())
    throw std::invalid_argument(
        "Scientific desktop documents are read-only until scientific editing is supported");
}
std::vector<std::size_t> Document::visibleRoiIndices() const {
  const auto &projected = get(state().metadata, "rois");
  const auto snapshot = nativeSnapshot();
  const auto &display = snapshot.display();
  const auto &rois = snapshot.data().rois;
  std::vector<std::size_t> visible;
  if (rois.empty())
    return visible;
  if (!projected.is_array() || projected.size() != rois.size())
    throw std::logic_error("Desktop ROI projection does not match native state");
  visible.reserve(rois.size());
  for (std::size_t index = 0; index < rois.size(); ++index) {
    const auto &roi = rois[index];
    if ((!roi.t || *roi.t == display.t) &&
        (!roi.c || *roi.c == display.c) &&
        (!roi.z || *roi.z == display.crosshair[2]))
      visible.push_back(index);
  }
  return visible;
}
Json Document::visibleRois() const {
  const auto indices = visibleRoiIndices();
  const auto &projected = get(state().metadata, "rois");
  Json visible = Json::array();
  for (const auto index : indices)
    visible.push_back(projected[index]);
  return visible;
}
FloatingSamplePolicy Document::samplePolicy() const {
  return nativeSnapshot().data().dataset.sample_policy();
}
ViewState Document::view() const { return nativeSnapshot().display(); }
Revisions Document::revisions() const { return nativeSnapshot().revisions(); }
std::uint64_t Document::revision() const {
  const auto r = revisions();
  return r.pixels + r.metadata + r.selection;
}
std::uint64_t Document::renderRevision() const {
  const auto r = revisions();
  return r.pixels + r.metadata + r.selection + r.display;
}
std::shared_ptr<imagejxx::Document> Document::nativeOwner() const {
  return native_;
}
void Document::bindController(
    std::shared_ptr<imagejxx::Application> controller) {
  auto snap = controller->snapshot(id);
  if (!snap.same_document(native_->snapshot()))
    throw std::invalid_argument("Controller has a different document owner");
  controller_ = std::move(controller);
}
void Document::releaseFrontend() { validation_.reset(); }
DocumentSnapshot Document::nativeSnapshot() const {
  return controller_ ? controller_->snapshot(id) : native_->snapshot();
}
void Document::markSaved() {
  if (controller_)
    controller_->mark_saved(id);
  else
    native_->mark_saved();
}
void Document::setHistoryPolicy(std::size_t limit, std::size_t budget) {
  if (controller_)
    controller_->set_history_policy(limit, budget, id);
  else
    native_->set_history_policy(limit, budget);
}
std::size_t Document::historyLimit() const { return native_->history_limit(); }
std::size_t Document::historyPixelBudget() const {
  return native_->history_pixel_budget();
}
std::size_t Document::undoSteps() const { return native_->undo_steps(); }
std::size_t Document::redoSteps() const { return native_->redo_steps(); }
int Document::slice() const { return view().crosshair[2]; }
void Document::admitView(bool orthogonal) const {
  const auto &dataset=nativeSnapshot().data().dataset;
  if(dataset.has_eager_planes())return;
  const auto s=dataset.shape();s.validate();
  auto fits=[&](int w,int h){
    std::size_t left=maxViewBytes_/sizeof(double);
    for(int extent:{s.s,w,h}){if(std::size_t(extent)>left)throw std::invalid_argument("Virtual decoded view buffer exceeds byte limit");left/=std::size_t(extent);}
  };
  // Even regional providers may use the default full-XY fallback.
  fits(s.x,s.y);
  if(orthogonal){fits(s.x,s.z);fits(s.y,s.z);}
}
std::shared_ptr<const Image> Document::sharedFrame() const {
  const auto snapshot=nativeSnapshot();const auto &dataset=snapshot.data().dataset;
  const PlanePosition position{snapshot.display().t,snapshot.display().c,snapshot.display().crosshair[2]};
  if(dataset.has_eager_planes()){auto pixels=dataset.plane(position);viewSlot_->clear();return pixels;}
  admitView();
  return viewCache_->read(viewSlot_,dataset,position);
}
const Image &Document::frame() const { return *sharedFrame(); }
std::string Document::title() const {
  return nativeSnapshot().data().metadata.title;
}
bool Document::dirty() const { return nativeSnapshot().dirty(); }
Json Document::pixelStorage() const {
  auto pixels =
      controller_ ? controller_->history(id).pixels : native_->history().pixels;
  return {{"logical_bytes", pixels.logical_bytes},
          {"retained_bytes", pixels.retained_bytes},
          {"plane_buffers", pixels.retained_planes}};
}
std::size_t Document::historyPixelBytes() const {
  return controller_ ? controller_->history(id).pixels.obsolete_bytes
                     : native_->history().pixels.obsolete_bytes;
}
void Document::normalizeView() {
  auto display = view();
  const auto before = display.crosshair;
  const int t = display.t, c = display.c;
  display.normalize(nativeSnapshot().data().dataset.shape());
  if (display.crosshair != before || display.t != t || display.c != c)
    setNativeDisplay(std::move(display));
}
Json Document::viewJson() const {
  const auto snapshot = nativeSnapshot();
  auto result = session_metadata::view_json(snapshot.display());
  if (scientificData(snapshot.data())) {
    result["t"] = snapshot.display().t;
    result["c"] = snapshot.display().c;
  }
  return result;
}
std::array<double, 3> Document::displaySpacing() const {
  const auto cal = spatialCalibration();
  std::array<double, 3> spacing{};
  for (int axis = 0; axis < 3; ++axis) {
    double length = 0;
    for (int row = 0; row < 3; ++row)
      length += cal.direction[row * 3 + axis] * cal.direction[row * 3 + axis];
    spacing[axis] = cal.spacing[axis] * std::sqrt(length);
    if (!std::isfinite(spacing[axis]) || spacing[axis] <= 0)
      throw std::invalid_argument("Unsupported physical display spacing");
  }
  return spacing;
}
void Document::fitSelection(std::array<double, 2> extent) {
  const auto state = this->state();
  const auto view = this->view();
  for (auto value : extent)
    if (!std::isfinite(value) || value <= 0)
      throw std::invalid_argument(
          "Selection fit needs a positive canvas extent");
  const auto &selected = get(state.metadata, "selection");
  if (selected.empty())
    throw std::invalid_argument("Draw a selection first");
  const auto &image = frame();
  const auto bounds = selectionBounds(selected, image.width, image.height);
  const auto spacing = displaySpacing();
  const double zoom = .96 * std::min(extent[0] / (bounds.width * spacing[0]),
                                     extent[1] / (bounds.height * spacing[1]));
  auto pan = view.pan;
  pan[0] = {
      (image.width / 2. - bounds.x - bounds.width / 2.) * spacing[0] * zoom,
      (image.height / 2. - bounds.y - bounds.height / 2.) * spacing[1] * zoom};
  if (view.linkedZoom)
    setView({{"zoom", zoom}, {"pan", pan}});
  else {
    auto zooms = view.planeZoom;
    zooms[0] = zoom;
    setView({{"plane_zoom", zooms}, {"pan", pan}});
  }
}
void Document::setView(const Json &parameters) {
  if (!parameters.is_object())
    throw std::invalid_argument("View parameters must be an object");
  const auto snapshot = nativeSnapshot();
  const auto shape = snapshot.data().dataset.shape();
  auto proposed = snapshot.display();
  auto camera = parameters;
  auto axis = [&](const char *name, int extent, int &value) {
    if (!parameters.contains(name))
      return;
    const auto &index = parameters.at(name);
    if (!index.is_number_integer())
      throw std::invalid_argument(std::string("View ") + name +
                                  " index must be an integer");
    if (index < 0 || index >= extent)
      throw std::out_of_range(std::string("View ") + name +
                              " index is outside the dataset");
    value = index.get<int>();
    camera.erase(name);
  };
  axis("t", shape.t, proposed.t);
  axis("c", shape.c, proposed.c);
  proposed = session_metadata::parse_view(
      camera, proposed, shape.x, shape.y, shape.z,
      snapshot.display().crosshair[2]);
  proposed.validate(shape);
  setNativeDisplay(std::move(proposed));
}
SpatialCalibration calibrationFromJson(const Json &value) {
  return session_metadata::calibration_from_json(value);
}

SpatialCalibration Document::spatialCalibration() const {
  return nativeSnapshot().data().dataset.calibration();
}
Image Document::plane(Plane orientation, int coordinate) const {
  admitView(orientation!=Plane::XY);
  const auto snapshot = nativeSnapshot();
  return snapshot.data().dataset.extract_plane(
      orientation, coordinate, snapshot.display().t, snapshot.display().c);
}
void Document::setNativeDisplay(DisplayState display) {
  recognized_channel_display(display, nativeSnapshot().data().dataset);
  if (controller_)
    controller_->set_display(std::move(display), id);
  else
    native_->set_display(std::move(display));
}
void Document::publishNativePaths(const std::function<void()> &publish) {
  if(std::this_thread::get_id()!=pathAdmission_->thread)
    throw std::runtime_error("Desktop documents must be edited on their creator thread");
  const bool before=pathAdmission_->enabled;
  pathAdmission_->enabled=true;
  try { publish(); } catch(...) {pathAdmission_->enabled=before;throw;}
}
void Document::commitPlanarNative(DocumentTransaction &transaction) {
  publishNativePaths([&]{if(controller_)controller_->commit(transaction);else native_->commit(transaction);});
}
void Document::commitPlanarAnnotation(DocumentTransaction &transaction) {
  if(transaction.axis_remap_display())
    throw std::invalid_argument("Planar annotation publication cannot override display/history state");
  const auto current=nativeSnapshot();
  if(transaction.document_id()!=id || !includes(transaction.dependencies(),Change::Pixels) ||
     !includes(transaction.dependencies(),Change::Metadata))
    throw std::invalid_argument("Planar annotation publication requires the captured owner and pixel/metadata dependencies");
  const auto preview=transaction.preview();
  auto allowed=current.data();
  const auto found=preview.data().metadata.extensions.find("imagejxx.planar_annotations");
  if(found==preview.data().metadata.extensions.end())allowed.metadata.extensions.erase("imagejxx.planar_annotations");
  else allowed.metadata.extensions["imagejxx.planar_annotations"]=found->second;
  if(document_changes(allowed,preview.data())!=Change::None)
    throw std::invalid_argument("Planar annotation publication cannot change pixels or other document state");
  commitPlanarNative(transaction);
}
void Document::commitDisplayAnnotation(DocumentTransaction &transaction) {
  if(transaction.axis_remap_display())
    throw std::invalid_argument("Display annotation publication cannot override display/history state");
  const auto current=nativeSnapshot();
  if(transaction.document_id()!=id || !includes(transaction.dependencies(),Change::Pixels) ||
     !includes(transaction.dependencies(),Change::Metadata))
    throw std::invalid_argument("Display annotation publication requires the captured owner and pixel/metadata dependencies");
  const auto preview=transaction.preview();
  auto allowed=current.data();
  const auto found=preview.data().metadata.extensions.find("imagejxx.planar_display_annotations");
  if(found==preview.data().metadata.extensions.end())allowed.metadata.extensions.erase("imagejxx.planar_display_annotations");
  else allowed.metadata.extensions["imagejxx.planar_display_annotations"]=found->second;
  if(document_changes(allowed,preview.data())!=Change::None)
    throw std::invalid_argument("Display annotation publication cannot change pixels or other document state");
  if(controller_)controller_->commit(transaction);
  else native_->commit(transaction);
}
void Document::commitImageJAnnotationImport(const ResolvedImageJAnnotationImport &command) {
  const auto current=nativeSnapshot();
  if(!command.source().same_document(current))throw std::invalid_argument("Mixed import belongs to another document");
  if(!current.revisions().matches(command.source().revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
  auto transaction=prepare_imagej_annotation_import(command);
  const auto preview=transaction.preview();auto allowed=current.data();
  for(const auto key:{planar_annotations_extension,planar_display_annotations_extension,imagej_roi_import_sources_extension}) {
    const auto found=preview.data().metadata.extensions.find(key);
    if(found==preview.data().metadata.extensions.end())allowed.metadata.extensions.erase(key);
    else allowed.metadata.extensions[key]=found->second;
  }
  if(document_changes(allowed,preview.data())!=Change::None)
    throw std::invalid_argument("Mixed import cannot change pixels or other document state");
  commitPlanarNative(transaction);
}
void Document::commitImageJRoiImport(const ResolvedImageJRoiImport &command) {
  const auto current=nativeSnapshot();
  if(!command.source().same_document(current))throw std::invalid_argument("ROI import belongs to another document");
  if(!current.revisions().matches(command.source().revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
  auto transaction=prepare_imagej_roi_import(command);
  const auto preview=transaction.preview();
  auto allowed=current.data();
  for(const auto key:{planar_annotations_extension,imagej_roi_import_sources_extension}) {
    const auto found=preview.data().metadata.extensions.find(key);
    if(found==preview.data().metadata.extensions.end())allowed.metadata.extensions.erase(key);
    else allowed.metadata.extensions[key]=found->second;
  }
  if(document_changes(allowed,preview.data())!=Change::None)
    throw std::invalid_argument("ROI import cannot change pixels or other document state");
  commitPlanarNative(transaction);
}
void Document::commitNative(DocumentTransaction &transaction) {
  requireLegacyEditable();
  if (controller_)
    controller_->commit(transaction);
  else
    native_->commit(transaction);
}
Document Document::snapshot() const {
  Document copy(nativeSnapshot(), WorkerTag{});
  copy.path = path;
  copy.maxViewBytes_=maxViewBytes_;
  copy.viewCache_->setBudget(viewCache_->budget());
  copy.origin_ =
      origin_ ? origin_ : std::optional<DocumentSnapshot>(nativeSnapshot());
  return copy;
}
void Document::checkpoint() {
  requireLegacyEditable();
  nativeSnapshot();
  native_->checkpoint();
}
void Document::undo() {
  // Native history validates frontend constraints before changing either stack.
  if (controller_)
    controller_->undo(id);
  else
    native_->undo();
}
void Document::redo() {
  if (controller_)
    controller_->redo(id);
  else
    native_->redo();
}
void Document::select(const Json &selection) {
  requireLegacyEditable();
  auto value = selection.empty()
                   ? std::optional<Selection>{}
                   : std::optional<Selection>(selectionFromJson(selection));
  if (controller_)
    controller_->select(std::move(value), id);
  else
    native_->select(std::move(value));
}
State Document::compute(const std::string &name, const Json &p, bool all,
                        ComponentCounts *counts) const {
  requireLegacyEditable();
  const auto state = this->state();
  State proposed = state;
  if (counts)
    counts->clear();
  const auto target =
      resolve_legacy_target(nativeSnapshot(), all, slice(), name);
  const auto computed = prepare(name, target, p);
  proposed.frames = *computed.preview().data().dataset.shared_planes();
  const bool rankVariance=name=="rank_filter_imagej" && p.value("method",std::string("median"))=="variance";
  if(rankVariance || (nativeSnapshot().data().metadata.extensions.count(indexed_palette_extension) &&
      (name=="convert_image" || !computed.preview().data().metadata.extensions.count(indexed_palette_extension)))){
    // Indexed type conversion can change only its palette. Carry the complete
    // native metadata and paired display, not just the legacy pixel table.
    const auto preview=computed.preview();proposed.metadata=nativeMetadata(preview.data());
    preserveDisplay(proposed.metadata,metadataJson(preview.display().properties));
    proposed.metadata["slice_index"]=preview.display().crosshair[2];
    proposed.spectrum=preview.data().spectrum;
    if(rankVariance)proposed.dependencies=Change::All;
    return proposed;
  }
  if (counts)
    for (const auto &output : computed.outputs())
      if (output.label_count)
        counts->push_back({output.position, *output.label_count});
  if (proposed.frames[0].width != frame().width ||
      proposed.frames[0].height != frame().height) {
    proposed.metadata.erase("selection");
    proposed.metadata["rois"] = Json::array();
    proposed.metadata["overlays"] = Json::array();
  }
  proposed.metadata.erase("display_range");
  proposed.metadata["labels"] = (name == "label" || name == "binary_watershed");
  proposed.spectrum.reset();
  proposed.metadata.erase("spectrum_shape");
  return proposed;
}

void Document::commit(State proposed) {
  requireLegacyEditable();
  const auto current = nativeSnapshot();
  const auto source = !origin_ && proposed.source ? *proposed.source : current;
  const auto prior = source.data().dataset.shared_planes();
  if (proposed.frames.empty())
    throw std::invalid_argument("Empty edit");
  for (std::size_t k = 0; k < proposed.frames.size(); ++k) {
    if ((!prior || k >= prior->size() ||
         proposed.frames.shared(k) != prior->shared(k)) &&
        !isCanonical(proposed.frames[k], proposed.frames.sample_policy()))
      proposed.frames.replace(
          k, canonicalSource(proposed.frames[k], proposed.frames.sample_policy()));
    require_same_shape(proposed.frames[0], proposed.frames[k]);
    if (proposed.frames[k].type != proposed.frames[0].type)
      throw std::invalid_argument("Mixed stack types");
  }
  if (proposed.frames.size() == std::size_t(current.data().dataset.shape().z))
    proposed.metadata["slice_index"] = current.display().crosshair[2];
  auto after = stateData(proposed, &source);
  auto edit = source.begin_edit(source.data().metadata.extensions.count(indexed_palette_extension)?Change::All:proposed.dependencies);
  const auto &before = source.data();
  if (!before.dataset.shares_pixels(after.dataset))
    edit.replace_dataset(after.dataset);
  edit.set_calibration(after.dataset.calibration());
  if (before.metadata.title != after.metadata.title)
    edit.set_title(after.metadata.title);
  auto patch = [](const auto &oldValues, const auto &newValues, auto set,
                  auto remove) {
    for (const auto &[key, value] : oldValues)
      if (!newValues.count(key))
        remove(key);
    for (const auto &[key, value] : newValues) {
      auto it = oldValues.find(key);
      if (it == oldValues.end() || !metadata_equal(it->second, value))
        set(key, value);
    }
  };
  patch(
      before.metadata.properties, after.metadata.properties,
      [&](const auto &key, const auto &value) {
        edit.set_property(key, value);
      },
      [&](const auto &key) { edit.remove_property(key); });
  patch(
      before.metadata.extensions, after.metadata.extensions,
      [&](const auto &key, const auto &value) {
        edit.set_extension(key, value);
      },
      [&](const auto &key) { edit.remove_extension(key); });
  if (before.selection != after.selection)
    edit.select(after.selection);
  if (before.previous_selection != after.previous_selection ||
      before.selection != after.selection)
    edit.set_previous_selection(after.previous_selection);
  if (before.rois != after.rois)
    edit.set_rois(after.rois);
  if (!sameOverlays(before.overlays, after.overlays))
    edit.set_overlays(after.overlays);
  if (before.spectrum != after.spectrum)
    edit.set_spectrum(after.spectrum);
  if (!metadata_equal(before.metadata.record_layout,
                      after.metadata.record_layout))
    edit.set_record_layout(after.metadata.record_layout);
  auto display =
      stateDisplay(proposed, source.display(), after.dataset.shape());
  if (proposed.dependencies == Change::All && after.dataset.pixel_type() != PixelType::UInt8)
    edit.set_axis_remap_display(display);
  if (controller_)
    controller_->commit(edit, display, true);
  else
    native_->commit(edit, display, true);
}
Json Document::measure(const Json &selected, int index) const {
  requireLegacyEditable();
  const auto state = this->state();
  if (index < 0)
    index = slice();
  if (index >= int(state.frames.size()))
    throw std::invalid_argument("Slice out of range");
  const auto &image = state.frames[index];
  auto selection =
      selected.empty() ? get(state.metadata, "selection") : selected;
  std::optional<Image> mask;
  if (!selection.empty())
    mask = selectionMask(selection, image.width, image.height);
  auto values = statistics(gray(image), mask ? &*mask : nullptr);
  auto c = get(state.metadata, "calibration");
  double sx = number(get(c, "pixel_width"), 1),
         sy = number(get(c, "pixel_height"), 1);
  Json row{{"Image", title()},
           {"Slice", index + 1},
           {"count", double(values.count)},
           {"minimum", values.minimum},
           {"maximum", values.maximum},
           {"mean", values.mean},
           {"stddev", values.stddev},
           {"median", values.median},
           {"sum", values.sum},
           {"integrated_density", values.integrated_density},
           {"Area", values.count * sx * sy},
           {"Unit", c["unit"]}};
  auto kind = string(get(selection, "kind"));
  if (!selection.empty()) {
    row["ROI"] = kind;
    auto r = selectionBounds(selection, image.width, image.height);
    row["Bounds"] = Json{r.x, r.y, r.width, r.height};
  }
  if (kind == "line" || kind == "polyline" || kind == "angle") {
    auto p = points(selection);
    double length = 0;
    for (size_t i = 1; i < p.size(); ++i)
      length += std::hypot((p[i][0] - p[i - 1][0]) * sx,
                           (p[i][1] - p[i - 1][1]) * sy);
    row["Length"] = length;
    if (kind == "angle" && p.size() == 3) {
      double ax = (p[0][0] - p[1][0]) * sx, ay = (p[0][1] - p[1][1]) * sy,
             bx = (p[2][0] - p[1][0]) * sx, by = (p[2][1] - p[1][1]) * sy;
      double norm = std::hypot(ax, ay) * std::hypot(bx, by);
      row["Angle"] =
          norm ? std::acos(std::clamp((ax * bx + ay * by) / norm, -1.0, 1.0)) *
                     180 / 3.141592653589793
               : 0;
    }
  }
  return row;
}
Json documentJson(const Document &d) {
  d.requireLegacyEditable();
  if (d.nativeSnapshot().display().properties.count("channel_display"))
    throw std::invalid_argument("Channel display requires native V3 sessions");
  auto item = d.state().metadata;
  item["view_state"] = d.viewJson();
  const auto &f = d.state().frames[0];
  Json shape = Json::array();
  if (boolean(get(item, "is_stack")))
    shape.push_back(d.state().frames.size());
  shape.push_back(f.height);
  shape.push_back(f.width);
  if (f.channels > 1)
    shape.push_back(f.channels);
  std::vector<double> values;
  for (const auto &frame : d.state().frames)
    values.insert(values.end(), frame.pixels.begin(), frame.pixels.end());
  item["dtype"] = typeName(f.type);
  item["shape"] = shape;
  const auto policy = d.samplePolicy();
  if (policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
      policy == FloatingSamplePolicy::PreserveIEEEFloat32) {
    if (f.type != PixelType::Float32 || f.channels != 1)
      throw std::invalid_argument("Session quiet-NaN policy requires grayscale Float32");
    item["pixel_sample_policy"] = policy==FloatingSamplePolicy::PreserveIEEEFloat32
        ? "preserve_ieee_float32" : "preserve_quiet_nan_float32";
  } else if (policy != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("Invalid floating sample policy");
  item["pixels"] = base64(rawDoubles(values, policy));
  if (d.state().spectrum) {
    const auto &fft = d.state().spectrum->spectrum();
    std::vector<double> complex;
    for (auto v : fft.pixels) {
      complex.push_back(v.real());
      complex.push_back(v.imag());
    }
    item["spectrum"] = base64(rawDoubles(complex));
    item["fft_shape"] = {fft.height, fft.width};
    if (d.state().spectrum->original_shape())
      item["spectrum_shape"] = *d.state().spectrum->original_shape();
  }
  return item;
}
std::shared_ptr<Document> documentFromJson(const Json &item) {
  if (!item.is_object())
    throw std::invalid_argument("Session document must be an object");
  if (item.contains("channel_display"))
    throw std::invalid_argument("Channel display requires native V3 sessions");
  validateNumericJson(item);
  const auto policy = sessionSamplePolicy(item);
  const auto type = pixelType(string(get(item, "dtype")));
  const auto &shape = get(item, "shape");
  bool stack = boolean(get(item, "is_stack"));
  size_t k = stack ? 1 : 0;
  if (!shape.is_array() || (shape.size() != k + 2 && shape.size() != k + 3))
    throw std::invalid_argument("Invalid session shape");
  int z = stack ? integer(shape[0]) : 1, h = integer(shape[k]),
      w = integer(shape[k + 1]),
      c = shape.size() == k + 3 ? integer(shape[k + 2]) : 1;
  if (z <= 0 || h <= 0 || w <= 0 || (c != 1 && c != 3 && c != 4) ||
      double(z) * h * w * c > 1024ULL * 1024 * 1024 / 8)
    throw std::invalid_argument("Invalid session dimensions");
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      (type != PixelType::Float32 || c != 1))
    throw std::invalid_argument("Session quiet-NaN policy requires grayscale Float32");
  auto values = decodeDoubles(get(item, "pixels"), size_t(z) * h * w * c,
                              policy);
  Stack frames;
  size_t count = size_t(h) * w * c;
  for (int i = 0; i < z; ++i) {
    Image f(w, h, c, type);
    std::copy_n(values.begin() + i * count, count, f.pixels.begin());
    frames.push_back(canonicalSource(std::move(f), policy));
  }
  auto d = std::make_shared<Document>(std::move(frames),
                                      string(get(item, "title"), "Untitled"),
                                      Json::object(), policy);
  State initial = d->state();
  initial.metadata.update(item);
  const auto &savedView = get(item, "view_state");
  if (!savedView.is_null()) {
    if (!savedView.is_object())
      throw std::invalid_argument("Saved view state must be an object");
    if (!savedView.empty())
      d->setView(savedView);
  }
  initial.metadata.erase("view_state");
  for (auto key : {"pixels", "dtype", "shape", "spectrum", "fft_shape",
                   "pixel_sample_policy"})
    initial.metadata.erase(key);
  int index = integer(get(item, "slice_index"));
  if (index < 0 || index >= z)
    throw std::invalid_argument("Invalid session slice index");
  const auto &selection = get(item, "selection");
  if (!selection.empty())
    selectionMask(selection, w, h);
  const auto &rois = get(item, "rois");
  if (!rois.is_null() && !rois.is_array())
    throw std::invalid_argument("Invalid session ROIs");
  for (const auto &roi : rois) {
    selectionMask(get(roi, "selection"), w, h);
    if (!get(roi, "slice_index").is_null()) {
      int slice = integer(get(roi, "slice_index"));
      if (slice < 0 || slice >= z)
        throw std::invalid_argument("Invalid ROI slice");
    }
  }
  const auto &cal = get(initial.metadata, "calibration");
  for (auto key :
       {"pixel_width", "pixel_height", "pixel_depth", "frame_interval"})
    if (number(get(cal, key)) <= 0)
      throw std::invalid_argument("Invalid calibration");
  calibrationFromJson(cal);
  const auto &overlays = get(item, "overlays");
  if (!overlays.is_null() && !overlays.is_array())
    throw std::invalid_argument("Invalid overlays");
  for (const auto &o : overlays) {
    points(o);
    color(string(get(o, "color"), "#ffcc00"));
    if (number(get(o, "width"), 1) <= 0 ||
        integer(get(o, "font_size"), 16) <= 0)
      throw std::invalid_argument("Invalid overlay size");
    const auto &geometry =
        string(get(o, "kind")) == "roi" ? get(o, "selection") : o;
    if (geometry.contains(roi_style_attribute)) {
      const auto styled = selectionFromJson(geometry);
      roi_style(styled);
      if (string(get(geometry, "kind")) != "text")
        styled.validate(w, h);
    }
  }
  if (item.contains("spectrum")) {
    const auto &fs = get(item, "fft_shape");
    if (!fs.is_array() || fs.size() != 2)
      throw std::invalid_argument("Invalid spectrum shape");
    int fh = integer(fs[0]), fw = integer(fs[1]);
    if (fh <= 0 || fw <= 0 || double(fh) * fw > 1024ULL * 1024 * 1024 / 16)
      throw std::invalid_argument("Invalid spectrum size");
    auto spectral = decodeDoubles(get(item, "spectrum"), size_t(fh) * fw * 2);
    Spectrum fft{fw, fh, {}};
    for (size_t i = 0; i < spectral.size(); i += 2)
      fft.pixels.emplace_back(spectral[i], spectral[i + 1]);
    std::optional<std::array<int, 2>> originalShape;
    if (!get(item, "spectrum_shape").is_null())
      originalShape = get(item, "spectrum_shape").get<std::array<int, 2>>();
    initial.spectrum = FourierState(std::move(fft), originalShape);
    initial.metadata.erase("spectrum_shape");
  }
  auto restored = std::make_shared<Document>(std::move(initial));
  if (!savedView.is_null() && !savedView.empty())
    restored->setView(savedView);
  return restored;
}
namespace {
Json serializedSessionMetadata(const Json &item) {
  Json result = Json::object();
  for (auto it = item.begin(); it != item.end(); ++it)
    if (it.key() != "pixels" && it.key() != "dtype" && it.key() != "shape" &&
        it.key() != "spectrum" && it.key() != "fft_shape" &&
        it.key() != "spectrum_shape" && it.key() != "pixel_sample_policy")
      result[it.key()] = it.value();
  return result;
}

Json serializedDisplayKeys(const Json &item) {
  if (!item.is_object())
    throw std::invalid_argument("Session document must be an object");
  Json keys = Json::array();
  for (const auto *key : {"display_range", "lut", "custom_lut", manual_threshold_property})
    if (item.contains(key))
      keys.push_back(key);
  return keys;
}

bool nativeStateNeedsExtension(const DocumentSnapshot &snapshot, const Json &item) {
  const auto planes = snapshot.data().dataset.shared_planes();
  if (!planes)
    throw std::invalid_argument("Desktop session requires immutable eager planes");
  // Infer legacy metadata from the plane count only. The shared core helper
  // does not construct datasets, rasterize geometry or inspect pixel buffers.
  auto inferred = session_metadata::initial_metadata(planes->size(), "Untitled");
  auto metadata = serializedSessionMetadata(item);
  metadata.erase("view_state");
  inferred.update(metadata);
  const auto legacy = session_metadata::infer_metadata(inferred);
  const auto display = session_metadata::display_properties(inferred);
  return !(legacy == snapshot.data().metadata) ||
         !metadata_equal(display, snapshot.display().properties);
}

void validateNativeDocumentState(const Json &extension, const Json &items) {
  if (!extension.is_object() || extension.size() != 2 ||
      !extension.contains("version") ||
      !extension["version"].is_number_integer() ||
      extension["version"] != 1 || !extension.contains("documents") ||
      !extension["documents"].is_array() ||
      extension["documents"].size() != items.size())
    throw std::invalid_argument("Invalid native session document state");
  for (std::size_t index = 0; index < items.size(); ++index) {
    const auto &entry = extension["documents"][index];
    if (!entry.is_object() || entry.size() != 2 ||
        !entry.contains("display_properties") || !entry.contains("metadata"))
      throw std::invalid_argument("Invalid native session document-state entry");
    const auto &keys = entry["display_properties"];
    if (!keys.is_array())
      throw std::invalid_argument("Display-property presence must be a key array");
    std::vector<std::string> seen;
    for (const auto &key : keys) {
      if (!key.is_string())
        throw std::invalid_argument("Display-property presence key must be a string");
      const auto name = key.get<std::string>();
      if ((name != "display_range" && name != "lut" && name != "custom_lut" &&
           name != manual_threshold_property) ||
          std::find(seen.begin(), seen.end(), name) != seen.end())
        throw std::invalid_argument("Invalid or duplicate display-property presence key");
      seen.push_back(name);
    }
    if (keys != serializedDisplayKeys(items[index]))
      throw std::invalid_argument("Display-property presence differs from serialized document");
    const auto &metadata = entry["metadata"];
    if (!metadata.is_object() || metadata.size() != 4 ||
        !metadata.contains("title") || !metadata["title"].is_string() ||
        !metadata.contains("properties") || !metadata["properties"].is_object() ||
        !metadata.contains("extensions") || !metadata["extensions"].is_object() ||
        !metadata.contains("record_layout") || !metadata["record_layout"].is_object())
      throw std::invalid_argument("Invalid native session document metadata");
    DocumentMetadata decoded;
    decoded.title = metadata["title"].get<std::string>();
    decoded.properties = metadataObjectFromJson(metadata["properties"]);
    decoded.extensions = metadataObjectFromJson(metadata["extensions"]);
    decoded.record_layout = metadataObjectFromJson(metadata["record_layout"]);
    decoded.validate();
  }
}

std::shared_ptr<Document> restoreNativeDocumentState(
    const Document &document, const Json &item, const Json &entry) {
  const auto source = document.nativeSnapshot();
  auto data = source.data();
  const auto &metadata = entry["metadata"];
  data.metadata.title = metadata["title"].get<std::string>();
  data.metadata.properties = metadataObjectFromJson(metadata["properties"]);
  data.metadata.extensions = metadataObjectFromJson(metadata["extensions"]);
  data.metadata.record_layout = metadataObjectFromJson(metadata["record_layout"]);
  auto display = source.display();
  for (const auto *key : {"display_range", "lut", "custom_lut", manual_threshold_property}) {
    if (item.contains(key))
      display.properties[key] = metadataFromJson(item[key]);
    else
      display.properties.erase(key);
  }
  validateDesktop(data, display,false,false,true);
  // Verify the declared native metadata generates the SAME existing wire
  // metadata/view, without calling documentJson or copying raw pixel buffers.
  auto generated = nativeMetadata(data);
  preserveDisplay(generated, metadataJson(display.properties));
  generated["slice_index"] = display.crosshair[2];
  generated["view_state"] = document.viewJson();
  const auto serialized = serializedSessionMetadata(item);
  for (const auto *key : {"pixels", "dtype", "shape", "spectrum", "fft_shape",
                          "spectrum_shape", "pixel_sample_policy"}) {
    generated.erase(key);
  }
  // Canonical sorted JSON preserves integral-versus-floating and signed-zero
  // lexemes. Positive signed/unsigned parser variants still have the same
  // integer lexeme. Numeric-coercing JSON equality could hide a contradiction.
  if (generated.dump() != serialized.dump())
    throw std::invalid_argument("Native session metadata/view contradicts its serialized document");
  // Construct a clean owner directly. A metadata transaction would create
  // revisions/history/dirty state while loading this immutable snapshot.
  return std::make_shared<Document>(std::make_shared<imagejxx::Document>(
      std::move(data), std::move(display)));
}
} // namespace

std::shared_ptr<Document> documentFromJson(const Json &item, const Json &nativeState) {
  const auto items = Json::array({item});
  const auto marker = Json{{"version", 1}, {"documents", Json::array({nativeState})}};
  validateNativeDocumentState(marker, items);
  const auto document = documentFromJson(item);
  return restoreNativeDocumentState(*document, item, nativeState);
}

void saveSession(const std::string &path, const Documents &documents,
                 const Json &results, const Json &workflow) {
  for (const auto &document : documents) {
    if (!document)
      throw std::invalid_argument("Session document is required");
    document->requireLegacyEditable();
    if (document->nativeSnapshot().display().properties.count("channel_display"))
      throw std::invalid_argument("Channel display requires native V3 sessions");
  }
  Json items = Json::array();
  for (const auto &d : documents)
    items.push_back(documentJson(*d));
  Json states = Json::array();
  bool needsExtension = false;
  for (std::size_t index = 0; index < documents.size(); ++index) {
    const auto snapshot = documents[index]->nativeSnapshot();
    needsExtension |= nativeStateNeedsExtension(snapshot, items[index]);
    const auto &metadata = snapshot.data().metadata;
    states.push_back({{"display_properties", serializedDisplayKeys(items[index])},
                     {"metadata", {{"title", metadata.title},
                                   {"properties", metadataJson(metadata.properties)},
                                   {"extensions", metadataJson(metadata.extensions)},
                                   {"record_layout", metadataJson(metadata.record_layout)}}}});
  }
  auto root = Json{
      {"format", "imagejxx-session"},
      {"version", 2},
      {"documents", items},
      {"results", results},
      {"workflow", workflow}};
  if (needsExtension)
    root["native_document_state"] = {{"version", 1}, {"documents", states}};
  validateNumericJson(root);
  const auto data = root.dump();
  if (data.size() > 1024ULL * 1024 * 1024)
    throw std::invalid_argument("Session exceeds 1 GiB limit");
  std::vector<unsigned char> out{'I', 'M', 'A', 'G', 'E',
                                 'J', 'X', 'X', '2', '\n'};
  for (int k = 3; k >= 0; --k)
    out.push_back(static_cast<unsigned char>(data.size() >> (k * 8)));
  uLongf bound = compressBound(static_cast<uLong>(data.size()));
  size_t start = out.size();
  out.resize(start + bound);
  if (compress2(out.data() + start, &bound,
                reinterpret_cast<const Bytef *>(data.data()),
                static_cast<uLong>(data.size()), Z_DEFAULT_COMPRESSION) != Z_OK)
    throw std::runtime_error("Session compression failed");
  out.resize(start + bound);
  writeBytes(path, out);
}
Documents loadSession(const std::string &path, Json &results, Json &workflow,
                       Json *extensions) {
  if (nativeSessionFile(path))
    return loadNativeSession(path, results, workflow, extensions);
  auto payload = readBytes(path);
  const std::string magic = "IMAGEJXX2\n";
  if (payload.size() < 15 ||
      !std::equal(magic.begin(), magic.end(), payload.begin()))
    throw std::invalid_argument("Not an imagejxx session");
  uint32_t size = 0;
  for (int k = 10; k < 14; ++k)
    size = (size << 8) | payload[k];
  if (size == 0 || size > 1024ULL * 1024 * 1024)
    throw std::invalid_argument("Invalid session size");
  std::vector<unsigned char> data(size);
  uLongf written = size;
  uLong source = static_cast<uLong>(payload.size() - 14);
  if (uncompress2(data.data(), &written, payload.data() + 14, &source) !=
          Z_OK ||
      written != size || source != payload.size() - 14)
    throw std::invalid_argument("Invalid compressed session");
  auto root = Json::parse(data.begin(), data.end());
  validateNumericJson(root);
  if (get(root, "format") != "imagejxx-session" || get(root, "version") != 2 ||
      !get(root, "documents").is_array())
    throw std::invalid_argument("Invalid session structure");
  const auto &items = get(root, "documents");
  const bool explicitNativeState = root.contains("native_document_state");
  if (explicitNativeState)
    validateNativeDocumentState(root["native_document_state"], items);
  Documents documents;
  for (std::size_t index = 0; index < items.size(); ++index) {
    const auto &item = items[index];
    auto document = documentFromJson(item);
    if (explicitNativeState)
      document = restoreNativeDocumentState(*document, item,
          root["native_document_state"]["documents"][index]);
    documents.push_back(std::move(document));
  }
  auto rows = get(root, "results"), steps = get(root, "workflow");
  if (!rows.is_array() || !steps.is_array())
    throw std::invalid_argument("Invalid session tables");
  Json nextExtensions = Json::object();
  for (auto field = root.begin(); field != root.end(); ++field)
    if (field.key() != "format" && field.key() != "version" &&
        field.key() != "documents" && field.key() != "results" &&
        field.key() != "workflow" && field.key() != "native_document_state")
      nextExtensions[field.key()] = field.value();
  if (!extensions && !nextExtensions.empty())
    throw std::invalid_argument("A session extension destination is required");
  results.swap(rows);
  workflow.swap(steps);
  if (extensions)
    extensions->swap(nextExtensions);
  return documents;
}
State runWorkflow(const Document &document, const Json &steps,
                  std::function<bool()> cancelled,
                  std::optional<TargetRequest> gaussian_rebind,
                  GaussianReplayObserver observer,
                  std::optional<TargetRequest> rank_rebind) {
  document.requireLegacyEditable();
  if (!steps.is_array())
    throw std::invalid_argument("Workflow steps must be an array");
  // Typed records carry an original source identity. Working copies have new
  // identities, so their caller must choose a new target explicitly. Validate
  // every typed record before any step runs, without inventing source IDs.
  for (const auto &step : steps) {
    if (step.is_object() && step.contains("schema")) {
      if (get(step, "schema") == "imagejxx.axis-extraction-command") {
        const auto record = metadataObjectFromJson(step);
        if (get(step, "version") == 2 &&
            get(step, "operation") == "axis_range_orientation")
          validate_axis_orientation_command_record(record);
        else validate_axis_extraction_command_record(record);
        throw std::invalid_argument(
            "Typed extraction records require explicit extraction replay");
      }
      if (get(step, "schema") == "imagejxx.channel-display-command") {
        validate_channel_display_command_record(metadataObjectFromJson(step));
        throw std::invalid_argument(
            "Typed channel-display records require explicit display replay");
      }
      if (get(step, "schema") == "imagejxx.selection-creation-command") {
        validate_selection_creation_command_record(metadataObjectFromJson(step));
        throw std::invalid_argument(
            "Typed selection records require explicit selection replay");
      }
      throw std::invalid_argument("Unsupported workflow schema");
    }
    if (step.is_object() && step.contains("dataset_policy")) {
      const auto &policy = step["dataset_policy"];
      if (!policy.is_string() || policy != "preserve_untargeted" ||
          get(step, "operation") != "threshold" || step.contains("schema_version"))
        throw std::invalid_argument(
            "Workflow dataset_policy requires threshold preserve_untargeted");
    }
    if (step.is_object() && step.contains("schema_version")) {
      if (get(step, "operation") == "gaussian_blur_imagej") {
        validate_gaussian_command_record(metadataObjectFromJson(step));
        if (!gaussian_rebind)
          throw std::invalid_argument(
              "Typed Gaussian workflow replay requires an explicit TargetRequest");
      } else if (get(step, "operation") == "rank_filter_imagej") {
        validate_rank_command_record(metadataObjectFromJson(step));
        if (!rank_rebind)
          throw std::invalid_argument(
              "Typed rank workflow replay requires an explicit TargetRequest");
      } else {
        throw std::invalid_argument("Unsupported typed workflow operation");
      }
    }
  }
  const auto origin = document.state().source;
  auto working = std::make_unique<Document>(document.snapshot());
  const auto liveDestinationId = origin ? origin->id() : document.id;
  const auto observe = [&](const char *phase, std::size_t index,
                           const Json *original = nullptr,
                           const ResolvedGaussianBlurCommand *resolved = nullptr,
                           std::uint64_t published = 0,
                           const OperationParameters *parameters = nullptr,
                           const ResolvedRankFilterCommand *rank = nullptr) noexcept {
    if (observer) {
      try {
        observer({phase, index, liveDestinationId, document.id, working->id,
                  original, resolved, published, parameters, rank});
      } catch (...) {
        // Observation must not alter validation, preparation or publication.
        // The host observer records its own failure so incomplete diagnostics
        // cannot qualify a replay case.
      }
    }
  };
  observe("working_copy_created", 0);
  std::unique_ptr<Application> typed_application;
  auto commands = catalog();
  std::size_t stepIndex = 0;
  for (const auto &step : steps) {
    const auto currentStep = stepIndex++;
    if (cancelled())
      throw std::runtime_error("Workflow cancelled");
    if (step.is_object() && step.contains("schema_version")) {
      if (!typed_application) {
        typed_application = std::make_unique<Application>();
        typed_application->adopt(working->nativeOwner());
      }
      const auto execute = [&](const auto &command) {
        const ResolvedGaussianBlurCommand *gaussian = nullptr;
        const ResolvedRankFilterCommand *rank = nullptr;
        if constexpr (std::is_same_v<std::decay_t<decltype(command)>,
                                     ResolvedGaussianBlurCommand>)
          gaussian = &command;
        else
          rank = &command;
        const auto parameters = command.parameters();
        observe("command_resolved", currentStep, &step, gaussian, 0, &parameters, rank);
        ExecutionControl control;
        control.checkpoint = [&] {
          if (cancelled())
            throw std::runtime_error("Workflow cancelled");
        };
        auto prepared = typed_application->prepare(
            string(get(step, "operation")), command.target(), parameters, {}, control);
        if (cancelled())
          throw std::runtime_error("Workflow cancelled");
        const auto published = typed_application->publish(prepared);
        observe("scratch_published", currentStep, &step, gaussian,
                published.document_id, &parameters, rank);
        if (published.created)
          working = std::make_unique<Document>(published.document);
      };
      if (get(step, "operation") == "gaussian_blur_imagej")
        execute(rebind_gaussian_command(working->nativeSnapshot(),
                                        metadataObjectFromJson(step), *gaussian_rebind));
      else
        execute(rebind_rank_command(working->nativeSnapshot(),
                                    metadataObjectFromJson(step), *rank_rebind));
      continue;
    }
    auto name = string(get(step, "operation"));
    auto found =
        std::find_if(commands.begin(), commands.end(),
                     [&](const Json &c) { return get(c, "id") == name; });
    if (found == commands.end())
      throw std::invalid_argument("Unknown workflow operation");
    const auto &params = get(step, "parameters");
    if (!params.is_object())
      throw std::invalid_argument("Workflow parameters must be an object");
    auto merged = defaults(*found, working->frame());
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (!merged.contains(it.key()))
        throw std::invalid_argument("Unknown workflow parameter: " + it.key());
      merged[it.key()] = it.value();
    }
    if (step.contains("selection"))
      working->select(get(step, "selection"));
    int index = integer(get(step, "slice_index"), working->slice());
    if (index < 0 || index >= int(working->state().frames.size()))
      throw std::invalid_argument("Recorded slice outside stack");
    auto crosshair = working->view().crosshair;
    crosshair[2] = index;
    working->setView({{"crosshair", crosshair}});
    if (step.contains("dataset_policy")) {
      // Dedicated manual threshold records must replay the same partial-type
      // policy as Apply. Untagged historical commands retain their original
      // whole-stack conversion behavior below.
      if (!typed_application) {
        typed_application = std::make_unique<Application>();
        typed_application->adopt(working->nativeOwner());
      }
      const auto target = resolve_legacy_target(
          working->nativeSnapshot(), boolean(get(step, "all_slices")), index, name);
      ExecutionControl control;
      control.checkpoint = [&] {
        if (cancelled())
          throw std::runtime_error("Workflow cancelled");
      };
      if (!merged.at("lower").is_number() ||
          (!merged.at("upper").is_null() && !merged.at("upper").is_number()))
        throw std::invalid_argument("Threshold workflow bounds must be numeric");
      OperationParameters threshold_parameters{
          {"lower", merged.at("lower").get<double>()},
          {"upper", std::monostate{}}};
      if (!merged.at("upper").is_null())
        threshold_parameters["upper"] = merged.at("upper").get<double>();
      ExecutionOptions threshold_options;
      threshold_options.reset_manual_threshold = true;
      auto prepared = typed_application->prepare(
          name, target, threshold_parameters, threshold_options, control);
      if (cancelled())
        throw std::runtime_error("Workflow cancelled");
      typed_application->publish(prepared);
      continue;
    }
    working->commit(
        working->compute(name, merged, boolean(get(step, "all_slices"))));
  }
  auto result = working->state();
  // A new-document target changes the scratch owner, never the original
  // publication source retained by this all-or-nothing proposal.
  result.source = origin;
  return result;
}
} // namespace imagejxx::desktop
