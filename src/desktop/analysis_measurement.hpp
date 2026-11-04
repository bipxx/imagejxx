#pragma once
#include "desktop.hpp"
#include <imagejxx/target_analysis.hpp>

namespace imagejxx::desktop {
bool captured_measurement_available(const DocumentSnapshot&);
bool captured_profile_available(const DocumentSnapshot&);
// Extra frontend row storage, separately admitted before any source acquisition.
std::size_t captured_measurement_row_bytes(const ResolvedTargetAnalysis&);
Json captured_measurement_rows(const ResolvedTargetAnalysis&,const TargetAnalysisResult&);
}
