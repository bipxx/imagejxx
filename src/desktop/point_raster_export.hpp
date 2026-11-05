#pragma once
#include "desktop.hpp"
#include <imagejxx/execution.hpp>
namespace imagejxx::desktop {
// Metadata/font/layout preparation acquires no source plane. Output/codec
// allowances are conservative logical working bounds, not process RSS.
class PreparedExportImage {
  struct State;
  std::unique_ptr<State> state_;
public:
  PreparedExportImage(const DocumentSnapshot&,bool point_show_all,std::size_t max_working_bytes);
  ~PreparedExportImage();
  std::size_t working_bytes() const;
  void validate(const DocumentSnapshot& current,bool point_show_all,std::size_t max_working_bytes) const;
  Image render(const ExecutionControl& = {}) const;
};
}
