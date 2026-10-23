#pragma once
#include <imagejxx/rgb_contrast_session.hpp>
namespace imagejxx::detail {
// Descriptor-only geometry validation shared by admission and closed records.
// No masks, sample acquisition, callbacks or copies of the selection.
void validate_rgb_contrast_session_selection(const std::optional<Selection> &,const DatasetShape &);
// Only after session admission. Shares exact active/other processor membership
// with preparation; never acquires samples. Used by metadata-only preflight.
bool rgb_contrast_session_has_membership(const DocumentSnapshot &,const RgbContrastSessionPlan &);
}
