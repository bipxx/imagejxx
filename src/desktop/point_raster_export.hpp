#pragma once
#include "desktop.hpp"
#include <imagejxx/execution.hpp>
namespace imagejxx::desktop {
using ExportAdmission=std::function<void(std::size_t)>;
enum class DisplayExportOutput { Pixels, Document };
struct DisplayExportFontPolicy { std::string default_path;std::filesystem::path directory; };
// Metadata/font/layout preparation acquires no source plane. Output/codec
// allowances are conservative logical working bounds, not process RSS.
class PreparedExportImage {
  struct State;
  std::unique_ptr<State> state_;
public:
  PreparedExportImage(DocumentSnapshot,bool point_show_all,std::size_t max_working_bytes,bool show_rois=false,
      DisplayExportOutput output=DisplayExportOutput::Document,ExportAdmission admit={},
      std::optional<DisplayExportFontPolicy> font_policy={});
  ~PreparedExportImage();
  std::size_t working_bytes() const;
  void validate(const DocumentSnapshot& current,bool point_show_all,std::size_t max_working_bytes) const;
  Image render(const ExecutionControl& = {}) const;
  // A detached single-plane RGB document; no active/saved/overlay annotations.
  // Metadata is captured/admitted before rendering, never copied from live state.
  std::shared_ptr<imagejxx::Document> document(Image) const;
};
}
