#pragma once

#include <imagejxx/document.hpp>

namespace imagejxx {

// Metadata-only prepared edits. Publish transaction and display together with
// Document::commit or Application::commit. All captured revisions must still
// match; a closed controller owner refuses. No pixels are read or replaced.
// Selection/reselection is transient: it does not dirty pixels/metadata or add
// history. Stored-row changes use one ordinary metadata history transaction.
struct PreparedRoiManagerEdit {
  DocumentTransaction transaction;
  std::optional<DisplayState> display;
  std::optional<std::size_t> row_index;
};

// Store captures explicit zero-based T/C/Z, including singleton-axis zeros.
// all_slices wildcards only Z; channel and time remain captured.
PreparedRoiManagerEdit prepare_store_roi(const DocumentSnapshot &source,
    std::string name = {}, bool all_slices = false);
// Update replaces geometry/style from the active selection, retaining the row's
// name, positions and opaque attributes. Properties changes those explicitly.
PreparedRoiManagerEdit prepare_update_roi(const DocumentSnapshot &, std::size_t);
PreparedRoiManagerEdit prepare_rename_roi(const DocumentSnapshot &, std::size_t,
                                        std::string name);
PreparedRoiManagerEdit prepare_delete_roi(const DocumentSnapshot &, std::size_t);
PreparedRoiManagerEdit prepare_select_roi(const DocumentSnapshot &, std::size_t);
// Explicit copies from a stored native XY annotation. Selection adoption
// navigates explicit T/C/Z; wildcard T/C retain the captured view. Storage
// preserves wildcard positions, row name/attributes and all selection metadata.
// Source annotations/provenance are unchanged. Non-XY rows refuse projection.
// Budget covers planar decoding/transfer staging, not unrelated transaction data.
PreparedRoiManagerEdit prepare_select_planar_annotation(const DocumentSnapshot &,
    std::size_t index, std::size_t max_working_bytes = 256 * 1048576);
PreparedRoiManagerEdit prepare_store_planar_annotation_roi(const DocumentSnapshot &,
    std::size_t index, std::size_t max_working_bytes = 256 * 1048576);
PreparedRoiManagerEdit prepare_roi_properties(const DocumentSnapshot &,
    std::size_t, std::string name, const MetadataObject &style_patch,
    std::optional<int> t, std::optional<int> c, std::optional<int> z);

} // namespace imagejxx
