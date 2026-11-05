#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
// Export-only captured visibility is typed, never a document metadata key.
Image renderExportImage(const Image&,const Json&,FloatingSamplePolicy,
                        PlanePosition,bool point_show_all);
}
