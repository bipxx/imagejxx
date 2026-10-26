#pragma once
#include <imagejxx/dataset_projection.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {
// Existing finite native numerical profile, with atomic return and immutable
// input. Unsupported sample policies fail before provider reads. No document
// annotations/display state are accepted or silently discarded by this API.
// Full ImageJ method/type/nonfinite and document/UI parity remain separate.
Dataset project_dataset_z(const Dataset &, const DatasetZProjectionRequest &,
                          const ExecutionControl & = {});
}
