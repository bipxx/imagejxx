#pragma once
#include <imagejxx/point_raster.hpp>
#include <memory>
#include <functional>
#include <exception>

namespace imagejxx::desktop {
namespace point_font_detail {
struct Budget {
  std::size_t limit,held=0,allocated=0,peak=0;
  bool denied=false;
  std::function<void(std::size_t)> admit;
  std::exception_ptr admission_failure;
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
  std::size_t max_bytes_,max_work_;
  std::function<void(std::size_t)> admit_;
public:
  explicit PointRasterFontCache(std::size_t max_bytes=8*1048576,std::size_t max_work=512*1048576,
      std::function<void(std::size_t)> admit={});
  ~PointRasterFontCache();
  // One explicitly captured file. Text methods do not re-resolve a path per glyph.
  void open(const std::string& path);
  int textBaseline(int size) const;
  std::shared_ptr<const PointRasterGlyph> textGlyph(int size,unsigned codepoint);
  std::size_t text_bytes() const;
  std::size_t working_bytes() const;
  std::size_t working_work() const;
  // One active file, at most seven sizes, owned for one overlay render call.
  const PointRasterFont& get(const std::string& path,double size);
  // Transfer admitted coverage without another bitmap copy. Charges remain
  // held for this resource lifetime, including transferred sizes.
  PointRasterFont take(const std::string& path,double size);
};
PointRasterFont loadPointRasterFont(const std::string& path,double size,
                                   std::size_t max_bytes=8*1048576);
}
