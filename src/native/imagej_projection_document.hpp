#pragma once
#include <imagejxx/imagej_projection_execution.hpp>
namespace imagejxx::detail {
struct ImageJProjectionDocument {
  DocumentData data;
  DisplayState display;
  ImageJDatasetProjectionLayout layout;
};
ImageJProjectionDocument project_imagej_document(
    const DocumentSnapshot&, const ImageJProjectionDocumentRequest&,
    const ExecutionControl&);
} // namespace imagejxx::detail
