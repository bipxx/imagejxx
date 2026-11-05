#pragma once
#include <imagejxx/point_raster.hpp>
#include <memory>

namespace imagejxx::desktop {
namespace point_font_detail {
struct Budget {
  std::size_t limit,held=0,allocated=0;
  bool denied=false;
  void retain(std::size_t bytes);
};
void* allocate(std::size_t bytes,void* user);
void release(void* memory,void* user);
}
// Same configured/platform resource policy as legacy text; no font is shipped.
std::string annotationFontPath(std::string configured={});
class PointRasterFontCache {
  friend PointRasterFont loadPointRasterFont(const std::string&,double,std::size_t);
  struct State;
  std::unique_ptr<State> state_;
  std::size_t max_bytes_;
public:
  explicit PointRasterFontCache(std::size_t max_bytes=8*1048576);
  ~PointRasterFontCache();
  // One active file, at most seven sizes, owned for one overlay render call.
  const PointRasterFont& get(const std::string& path,double size);
  // Transfer admitted coverage without another bitmap copy. Charges remain
  // held for this resource lifetime, including transferred sizes.
  PointRasterFont take(const std::string& path,double size);
};
PointRasterFont loadPointRasterFont(const std::string& path,double size,
                                   std::size_t max_bytes=8*1048576);
}
