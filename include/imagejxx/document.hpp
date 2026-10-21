#pragma once

#include <functional>
#include <imagejxx/dataset.hpp>
#include <imagejxx/metadata.hpp>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <variant>

namespace imagejxx {
struct SavedRoiGeometryLimits;

enum class Change : unsigned {
  None = 0,
  Pixels = 1,
  Metadata = 2,
  Selection = 4,
  Display = 8,
  Data = 7,
  All = 15
};
Change operator|(Change a, Change b);
bool includes(Change set, Change part);
struct Revisions {
  std::uint64_t pixels = 0, metadata = 0, selection = 0, display = 0;
  void advance(Change changes);
  bool matches(const Revisions &expected,
               Change dependencies = Change::Data) const;
};
class RevisionConflict : public std::runtime_error {
public:
  RevisionConflict()
      : std::runtime_error("Document changed since the edit began") {}
};
std::uint64_t next_document_id();
struct DocumentIdentity {
  const std::uint64_t id = next_document_id();
};

// Legacy XY geometry uses image-cell boundary coordinates. Dataset physical
// coordinates use voxel centers, so a boundary coordinate maps to voxel - .5.
struct SelectionFrame {
  std::array<double, 2> center, u, v; // center and two perpendicular half-axes
  std::array<double, 2> image_point(std::array<double, 2> local) const;
  std::array<double, 2> local_point(std::array<double, 2> image) const;
  std::array<double, 4> bounds(bool ellipse = false) const;
  void validate() const;
};
struct Selection {
  std::string kind;
  std::vector<std::array<double, 2>> points;
  double width = 1;
  MetadataObject attributes;
  Selection(std::string kind, std::vector<std::array<double, 2>> points = {},
            double width = 1);
  static Selection from_mask(const Image &mask);
  static Selection from_packed_mask(int width, int height,
                                    std::vector<unsigned char> bytes);
  int mask_width() const { return maskWidth_; }
  int mask_height() const { return maskHeight_; }
  const std::vector<unsigned char> &packed_mask() const;
  // Rotated boxes store center, center+u, center+v. Legacy boxes keep two
  // corners.
  std::optional<SelectionFrame> box_frame() const;
  // Rounded kinds retain editable geometry; the required diameter is stored as
  // an exact integer attribute and is never inferred from opaque legacy data.
  bool is_rounded_box() const;
  int corner_diameter() const;
  std::array<double, 44> rounded_path(bool processing = false) const;
  void validate(int width, int height) const;
  Image mask(int width, int height) const;
  bool operator==(const Selection &other) const;
  bool operator!=(const Selection &other) const { return !(*this == other); }

private:
  int maskWidth_ = 0, maskHeight_ = 0;
  std::shared_ptr<const std::vector<unsigned char>> mask_;
};
enum class SelectionBooleanOperation { Union, Intersection, Subtraction, Xor };
// Area operands use full-canvas raster membership. Subtraction keeps the first
// operand minus every subsequent operand. Empty results remain explicit masks.
Selection combine_selections(int width, int height,
                             const std::vector<Selection> &operands,
                             SelectionBooleanOperation operation);
struct Roi {
  std::string name;
  Selection selection;
  std::optional<int> t, c, z;
  MetadataObject attributes;
  void validate(DatasetShape shape) const;
  bool operator==(const Roi &other) const;
};
struct DocumentMetadata {
  std::string title = "Untitled";
  std::map<std::string, MetadataValue> properties;
  MetadataObject extensions;
  // Codec field-presence information; canonical values stay in typed records.
  MetadataObject record_layout;
  void validate() const;
  bool operator==(const DocumentMetadata &other) const;
};
struct DisplayState {
  int t = 0, c = 0;
  bool orthogonal = false, linkedZoom = true;
  std::array<int, 3> crosshair{0, 0, 0};
  double zoom = 0; // logical screen pixels per physical unit; zero means fit
  std::array<double, 3> planeZoom{};
  std::array<std::array<double, 2>, 3> pan{};
  std::uint64_t revision = 0;
  MetadataObject properties;
  void validate(DatasetShape shape) const;
  void normalize(DatasetShape shape);
};
// Validated once on ingestion, with immutable shared complex buffers. Original
// shape is Y,X and records the unpadded source extent for inverse FFT cropping.
class FourierState {
public:
  explicit FourierState(
      Spectrum spectrum,
      std::optional<std::array<int, 2>> original_shape = std::nullopt);
  const Spectrum &spectrum() const { return *spectrum_; }
  std::optional<std::array<int, 2>> original_shape() const {
    return originalShape_;
  }
  bool shares_storage(const FourierState &other) const {
    return spectrum_ == other.spectrum_;
  }
  bool operator==(const FourierState &other) const;
  bool operator!=(const FourierState &other) const { return !(*this == other); }

private:
  std::shared_ptr<const Spectrum> spectrum_;
  std::optional<std::array<int, 2>> originalShape_;
};
struct DocumentData {
  Dataset dataset;
  DocumentMetadata metadata;
  std::optional<Selection> selection;
  std::vector<Roi> rois;
  std::vector<MetadataObject> overlays;
  // Saved geometry can belong to an earlier extent; restore must validate it
  // against the current dataset before selecting it.
  std::optional<Selection> previous_selection;
  std::optional<FourierState> spectrum;
  void validate() const;
};
Change document_changes(const DocumentData &before, const DocumentData &after);
class DocumentTransaction;
class DocumentSnapshot {
public:
  // Adapter entry point: data remains immutable and retained by this snapshot.
  DocumentSnapshot(std::shared_ptr<const DocumentIdentity> identity,
                   Revisions revisions,
                   std::shared_ptr<const DocumentData> data,
                   DisplayState display, bool dirty = false,
                   std::optional<std::uint64_t> data_identity = std::nullopt);
  std::uint64_t id() const { return identity_->id; }
  bool same_document(const DocumentSnapshot &other) const {
    return identity_ == other.identity_;
  }
  Revisions revisions() const { return revisions_; }
  const DocumentData &data() const { return *data_; }
  const DisplayState &display() const { return display_; }
  // Detached presentation state, retaining the same source/data identity.
  DocumentSnapshot with_display(DisplayState display) const;
  bool dirty() const { return dirty_; }
  std::optional<std::uint64_t> data_identity() const { return dataIdentity_; }
  bool shares_data(const DocumentSnapshot &other) const {
    return data_ == other.data_;
  }
  DocumentTransaction begin_edit(Change dependencies = Change::Data) const;

private:
  friend class Document;
  friend class DocumentTransaction;
  std::shared_ptr<const DocumentIdentity> identity_;
  Revisions revisions_;
  std::shared_ptr<const DocumentData> data_;
  DisplayState display_;
  bool dirty_;
  std::optional<std::uint64_t> dataIdentity_;
};
class DocumentTransaction {
public:
  explicit DocumentTransaction(const DocumentSnapshot &source,
                               Change dependencies = Change::Data);
  std::uint64_t document_id() const { return source_.id(); }
  bool active() const { return active_; }
  Change dependencies() const { return dependencies_; }
  Revisions source_revisions() const { return source_.revisions(); }
  DocumentSnapshot preview() const;
  void replace_plane(PlanePosition position, Image image);
  void replace_dataset(Dataset dataset);
  void set_calibration(SpatialCalibration calibration);
  void set_title(std::string title);
  void set_property(std::string name, MetadataValue value);
  void remove_property(std::string name);
  void set_extension(std::string name, MetadataValue value);
  void remove_extension(std::string name);
  void select(std::optional<Selection> selection);
  void set_previous_selection(std::optional<Selection> selection);
  void set_rois(std::vector<Roi> rois);
  void set_overlays(std::vector<MetadataObject> overlays);
  void set_spectrum(std::optional<FourierState> spectrum);
  void set_record_layout(MetadataObject layout);
  // Axis/type-changing edits opt into paired display/history restoration. Ordinary
  // pixel/metadata edits retain their existing transient-camera Undo policy.
  void set_axis_remap_display(DisplayState display);
  const std::optional<DisplayState> &axis_remap_display() const { return axisDisplay_; }
  void rollback();
  // The publisher must commit atomically and hold its owner's lock/thread.
  // Failed preparation/publication leaves this transaction active and
  // retryable.
  void commit_to(const DocumentSnapshot &current,
                 const std::function<void(DocumentData, Change)> &publish);

private:
  friend class PreparedOperation;
  void retain_resources(std::shared_ptr<const void> guard);
  void require_active() const;
  DocumentData apply_to(const DocumentData &base) const;
  DocumentSnapshot source_;
  DocumentData staged_;
  Change dependencies_;
  bool active_ = true, pixels_ = false, calibration_ = false, title_ = false,
       selection_ = false, rois_ = false, overlays_ = false, spectrum_ = false,
       wholeDataset_ = false, recordLayout_ = false, previousSelection_ = false;
  std::map<std::array<int, 3>, std::shared_ptr<const Image>> planes_;
  std::map<std::string, std::optional<MetadataValue>> properties_;
  std::map<std::string, std::optional<MetadataValue>> extensions_;
  std::optional<DisplayState> axisDisplay_;
};
// Saved ROI positions do not restrict manager geometry combinations. Distinct,
// ordered indices capture this owner and its Data revisions; publication changes
// only the transient selection. Saving the result is a separate ROI edit.
DocumentTransaction
prepare_roi_boolean(const DocumentSnapshot &source,
                    const std::vector<std::size_t> &ordered_indices,
                    SelectionBooleanOperation operation);
// These figures count immutable eager plane buffers held by this document and
// its history, excluding externally retained snapshots, spectra and scratch.
// exact is false if any retained source does not expose an eager plane table.
struct PixelStorage {
  std::size_t logical_bytes = 0, current_bytes = 0, retained_bytes = 0,
              obsolete_bytes = 0, retained_planes = 0;
  bool exact = true;
};
struct HistoryInfo {
  std::size_t undo_steps = 0, redo_steps = 0, step_limit = 0, pixel_budget = 0;
  PixelStorage pixels;
};
struct HistoryPolicy {
  std::size_t step_limit = 8, pixel_budget = 0;
};
using DocumentValidator =
    std::function<void(const DocumentData &, const DisplayState &)>;
// Concurrent snapshots and commits are serialized; work uses an immutable
// snapshot outside the lock. Custom PlaneSource implementations must themselves
// support the concurrent reads their callers request.
class Document {
public:
  explicit Document(Dataset dataset, std::string title = "Untitled");
  explicit Document(DocumentData data,
                    std::optional<DisplayState> display = std::nullopt);
  // A new owner with shared immutable storage and complete retained records.
  explicit Document(const DocumentSnapshot &source);
  const std::shared_ptr<const DocumentIdentity> identity =
      std::make_shared<const DocumentIdentity>();
  const std::uint64_t id = identity->id;
  DocumentSnapshot snapshot() const;
  DocumentTransaction begin_edit(Change dependencies = Change::Data) const;
  void commit(DocumentTransaction &transaction,
              std::optional<DisplayState> display = std::nullopt,
              bool preserve_concurrent_display = false);
  void set_display(DisplayState display);
  // Atomically require the same owner and every captured revision, then
  // publish one Display revision even when the requested state is equal.
  // Validation/conflict failure leaves display, data and history unchanged.
  void set_display_if_current(const DocumentSnapshot &expected,
                              DisplayState display);
  // A frontend holds the returned lease while its capabilities/thread rules
  // apply. Validators must not call back into this document or its controller.
  std::shared_ptr<const DocumentValidator>
  constrain(DocumentValidator validator);
  // Compatibility checkpoint for initial publication; ordinary edits use TXs.
  void checkpoint();
  void select(std::optional<Selection> selection);
  void undo();
  void redo();
  void mark_saved();
  void mark_saved(const DocumentSnapshot &saved);
  void set_history_limit(std::size_t limit);
  void set_history_policy(std::size_t limit, std::size_t pixel_budget = 0);
  HistoryInfo history() const;
  std::size_t history_limit() const;
  std::size_t history_pixel_budget() const;
  std::size_t undo_steps() const;
  std::size_t redo_steps() const;

private:
  friend class PreparedOperation;
  friend class Application;
  friend class PreparedHistoryPolicies;
  struct SavedRoiGeometryRetirement;
  // Only Application can mint and consume this narrow publication guard.
  // It is checked under the same mutex as transaction/history mutation.
  struct SavedRoiGeometryGuard {
    const DocumentSnapshot &source;
    std::size_t row_index;
    const Roi &original;
    bool pointer;
    std::size_t history_bytes;
    SavedRoiGeometryRetirement &retirement;
    const SavedRoiGeometryLimits &limits;
    std::size_t admitted_display_bytes, working_bytes;
  };
  std::size_t saved_roi_geometry_history_bytes() const;
  std::size_t saved_roi_geometry_history_bytes_locked() const;
  void commit_saved_roi_geometry_guarded(
      DocumentTransaction &transaction, const SavedRoiGeometryGuard &guard);
  struct HistoryEntry {
    std::shared_ptr<const DocumentData> data;
    std::uint64_t identity;
    std::shared_ptr<const DisplayState> axis_display;
  };
  struct HistoryPolicyCandidate {
    std::vector<HistoryEntry> undo, redo;
    HistoryPolicy policy;
    std::optional<DisplayState> display;
    Revisions revisions;
  };
  HistoryPolicyCandidate prepare_history_policy_locked(
      HistoryPolicy policy, const DocumentSnapshot *expected = nullptr,
      const DisplayState *initial_display = nullptr) const;
  void publish_history_policy_locked(HistoryPolicyCandidate &candidate) noexcept;
  // Receives existing vectors by no-throw swap; Application destroys them
  // after both owner locks are released. No new allocation or callback hook.
  struct SavedRoiGeometryRetirement {
    std::vector<HistoryEntry> undo, redo;
  };
  void commit_impl(DocumentTransaction &transaction,
                   std::optional<DisplayState> display,
                   bool preserve_concurrent_display,
                   bool reset_manual_threshold,
                   const SavedRoiGeometryGuard *saved_roi_guard = nullptr);
  DocumentSnapshot snapshot_locked() const;
  void set_display_locked(DisplayState display);
  static PixelStorage pixel_storage(const DocumentData &current,
                                    const std::vector<HistoryEntry> &undo,
                                    const std::vector<HistoryEntry> &redo);
  static void prune_history(std::vector<HistoryEntry> &undo,
                            std::vector<HistoryEntry> &redo,
                            const DocumentData &current, std::size_t limit,
                            std::size_t pixel_budget);
  void set_history_policy_locked(std::size_t limit, std::size_t pixel_budget);
  void validate_constraints(const DocumentData &data,
                            const DisplayState &display) const;
  mutable std::mutex mutex_;
  std::shared_ptr<const DocumentData> data_;
  DisplayState display_;
  Revisions revisions_;
  std::vector<HistoryEntry> undo_, redo_;
  std::uint64_t identity_ = 0, savedIdentity_ = 0, nextIdentity_ = 0;
  std::size_t historyLimit_ = 8;
  std::size_t historyPixelBudget_ = 0;
  std::vector<std::weak_ptr<const DocumentValidator>> validators_;
};
} // namespace imagejxx
