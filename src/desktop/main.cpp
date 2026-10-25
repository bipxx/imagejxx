#include "shadow_ui_probe.hpp"
#include "histogram_workflow_ui_probe.hpp"
#include "animation_ui_probe.hpp"
#include "saved_roi_budget_ui_probe.hpp"
#include "rounded_legacy_ui_probe.hpp"
#include "rounded_roi_import_ui_probe.hpp"
#include "rounded_roi_export_ui_probe.hpp"
#include "ieee_viewing_ui_probe.hpp"
#include "rgb_dataset_ui_probe.hpp"
#include "axis_range_slicer_resize_ui_probe.hpp"
#include "window.hpp"
#include "scientific_io_ui_probe.hpp"
#include "roi_import_ui_probe.hpp"
#include "manual_threshold_ui_probe.hpp"
#include "scientific_session_ui_probe.hpp"
#include "scientific_navigation_ui_probe.hpp"
#include "scientific_operation_targets_ui_probe.hpp"
#include "scientific_slicing_ui_probe.hpp"
#include "scientific_orientation_ui_probe.hpp"
#include "channel_display_ui_probe.hpp"
#include "auto_contrast_ui_probe.hpp"
#include "selection_creation_ui_probe.hpp"
#include "scientific_roi_manager_ui_probe.hpp"
#include "z_subset_ui_probe.hpp"
#include "axis_spacing_ui_probe.hpp"
#include "slicer_resize_ui_probe.hpp"
#include "planar_annotation_ui_probe.hpp"
#include "planar_path_ui_probe.hpp"
#include "root_path_ui_probe.hpp"
#include "path_segment_ui_probe.hpp"
#include "path_authoring_ui_probe.hpp"
#include "path_authoring_edge_ui_probe.hpp"
#include "settings_wand_ui_probe.hpp"
#include "manual_threshold_workflow_ui_probe.hpp"
#include "path_filter_ui_probe.hpp"
#include "native_bare_import_ui_probe.hpp"
#include "planar_annotation_matrix.hpp"
#include "rank_ui_probe.hpp"
#include "rank_runtime_probe.hpp"
#include "saved_roi_geometry_ui_probe.hpp"
#include "shortcuts_ui_probe.hpp"
#include "history_settings_ui_probe.hpp"
#include "startup_ui_probe.hpp"
#include <chrono>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace {
int run(const std::vector<std::string> &arguments) {
  if (!arguments.empty() && arguments[0] == "--scientific-roi-manager-ui-case") {
    if (arguments.size() != 2)
      throw std::invalid_argument("Use --scientific-roi-manager-ui-case FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::scientificRoiManagerUiCase(editor, "imgui_cpp", arguments[1]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--startup-workspace-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --startup-workspace-ui-case SU01..SU10 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::startupUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--scientific-orientation-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --scientific-orientation-ui-case O01..O12 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::scientificOrientationUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--history-settings-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --history-settings-ui-case HU01..HU04 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::historySettingsUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--shortcuts-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --shortcuts-ui-case K01..K14 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::shortcutsUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--saved-roi-geometry-ui-dependencies") {
    if (arguments.size() != 2)
      throw std::invalid_argument("Use --saved-roi-geometry-ui-dependencies FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::savedRoiGeometryUiDependencies(editor, "imgui_cpp", arguments[1]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--saved-roi-geometry-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --saved-roi-geometry-ui-case PX01..PX10 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::savedRoiGeometryUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--saved-roi-geometry-ui-test") {
    if (arguments.size() != 2)
      throw std::invalid_argument("Use --saved-roi-geometry-ui-test FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    return imagejxx::desktop::savedRoiGeometryUiConsumerTest(editor, arguments[1]);
  }
  if (!arguments.empty() && arguments[0] == "--rank-runtime-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --rank-runtime-case PLAN_JSON FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::rankRuntimeCase(editor,
        imagejxx::desktop::readJson(arguments[1]), "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--ieee-viewing-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --ieee-viewing-ui-case IEEE01..IEEE04 FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::ieeeViewingUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--rank-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --rank-ui-case R01..R59 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::rankUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--auto-contrast-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --auto-contrast-ui-case U01..U08 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::autoContrastUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--selection-creation-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --selection-creation-ui-case S01..S16|R01..R03|Q01..Q04 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::selectionCreationUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--selection-creation-ui-test") {
    if (arguments.size() != 2)
      throw std::invalid_argument("Use --selection-creation-ui-test FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    return imagejxx::desktop::selectionCreationUiConsumerTest(editor, arguments[1]);
  }
  if (!arguments.empty() && arguments[0] == "--channel-display-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --channel-display-ui-case C01A..C06E|M01..M10 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::channelDisplayUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--channel-display-ui-test") {
    if (arguments.size() != 2)
      throw std::invalid_argument("Use --channel-display-ui-test FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    return imagejxx::desktop::channelDisplayUiConsumerTest(editor, arguments[1]);
  }
  if (!arguments.empty() && arguments[0] == "--scientific-slicing-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --scientific-slicing-ui-case E01..E10 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::scientificSlicingUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--scientific-slicing-ui-test") {
    if (arguments.size() != 2)
      throw std::invalid_argument("Use --scientific-slicing-ui-test FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    return imagejxx::desktop::scientificSlicingUiConsumerTest(editor, arguments[1]);
  }
  if (!arguments.empty() && arguments[0] == "--scientific-operation-targets-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --scientific-operation-targets-ui-case CASE_ID FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::scientificOperationTargetsUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--scientific-operation-targets-ui-test") {
    if (arguments.size() != 2)
      throw std::invalid_argument("Use --scientific-operation-targets-ui-test FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    return imagejxx::desktop::scientificOperationTargetsUiConsumerTest(editor, arguments[1]);
  }
  if (!arguments.empty() && arguments[0] == "--scientific-navigation-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --scientific-navigation-ui-case N01..N06 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::scientificNavigationUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "--scientific-session-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --scientific-session-ui-case S01..S03 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::scientificSessionUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  if (!arguments.empty() && (arguments[0] == "--z-subset-ui-case" || arguments[0] == "--regional-z-subset-ui-case" || arguments[0] == "--display-z-subset-ui-case")) {
    if(arguments.size()!=2)throw std::invalid_argument("Use --z-subset-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::zSubsetUiCase(editor,"imgui_cpp",arguments[1],arguments[0]=="--regional-z-subset-ui-case",arguments[0]=="--display-z-subset-ui-case");return 0;
  }
  if (!arguments.empty() && arguments[0] == "--axis-spacing-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --axis-spacing-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::axisSpacingUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--slicer-resize-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --slicer-resize-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::slicerResizeUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--planar-annotation-matrix-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --planar-annotation-matrix-ui-case CASE/ALL/GEOMETRY/ELLIPSE/ROTATED_BOX/ROTATED_ELLIPSE FRESH_DIRECTORY");
    const std::array<std::string,5> groups{"ALL","GEOMETRY","ELLIPSE","ROTATED_BOX","ROTATED_ELLIPSE"};
    const auto group=std::find(groups.begin(),groups.end(),arguments[1]);
    if(group!=groups.end()) {imagejxx::desktop::planarAnnotationMatrixUiBatch("imgui_cpp",arguments[2],int(group-groups.begin()));return 0;}
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::planarAnnotationMatrixUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-import-limits-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-import-limits-ui-case FRESH_DIRECTORY");
    auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());imagejxx::desktop::roiImportLimitsUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--mixed-export-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --mixed-export-ui-case FRESH_DIRECTORY");
    auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());imagejxx::desktop::roiMixedExportUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--native-workflow-batch-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --native-workflow-batch-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());imagejxx::desktop::nativeWorkflowBatchUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--float32-tiff-ui-case") {
    if(arguments.size()!=4)throw std::invalid_argument("Use --float32-tiff-ui-case TF01..TF06 FRESH_DIRECTORY FIXTURE_TIFF");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::float32TiffUiCase(editor,arguments[1],"imgui_cpp",arguments[2],arguments[3]);return 0;
  }
  if(!arguments.empty() && arguments[0]=="--virtual-tiff-ui-case"){
    if(arguments.size()!=3)throw std::invalid_argument("Use --virtual-tiff-ui-case MODE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::virtualTiffUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--scientific-tiff-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --scientific-tiff-ui-case FRESH_DIRECTORY FIXTURE_TIFF");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::scientificTiffUiCase(editor,"imgui_cpp",arguments[1],arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--rgb-dataset-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --rgb-dataset-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::rgbDatasetUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--scientific-process-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --scientific-process-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::scientificProcessUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--histogram-workflow-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --histogram-workflow-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::histogramWorkflowUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--native-workflow-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --native-workflow-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());imagejxx::desktop::nativeWorkflowUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--bare-edit-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --bare-edit-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());imagejxx::desktop::roiBareEditUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--mixed-import-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --mixed-import-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());imagejxx::desktop::roiMixedImportUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-import-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-import-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiImportUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--display-annotation-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --display-annotation-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::displayAnnotationUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-zip-export-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-zip-export-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiZipExportUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--rounded-roi-export-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --rounded-roi-export-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roundedRoiExportUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-export-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-export-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiExportUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--animation-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --animation-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::animationUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--shadow-demo-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --shadow-demo-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::shadowDemoUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--shadow-preset-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --shadow-preset-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::shadowPresetUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--scientific-shadow-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --scientific-shadow-ui-case DIRECTION FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::scientificShadowUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--shadow-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --shadow-ui-case DIRECTION FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::shadowUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--saved-roi-pointer-budget-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --saved-roi-pointer-budget-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::savedRoiPointerBudgetUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--saved-roi-budget-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --saved-roi-budget-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::savedRoiBudgetUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--rounded-legacy-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --rounded-legacy-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roundedLegacyUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--rounded-roi-import-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --rounded-roi-import-ui-case CASE FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roundedRoiImportUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-import-replacement-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-import-replacement-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiImportReplacementUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-import-rebind-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-import-rebind-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiImportRebindUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-import-jobs-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-import-jobs-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiImportJobsUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-import-browse-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-import-browse-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiImportBrowseUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--roi-import-queue-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --roi-import-queue-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::roiImportQueueUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--native-bare-import-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --native-bare-import-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::nativeBareImportUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--manual-threshold-workflow-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("--manual-threshold-workflow-ui-case CASE_ID FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::manualThresholdWorkflowUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--settings-wand-ui-case") {
    if(arguments.size()!=4 || arguments[3].empty())
      throw std::invalid_argument("--settings-wand-ui-case CASE_ID FRESH_DIRECTORY PREFERENCES_PATH");
    const auto preferences=std::filesystem::absolute(std::filesystem::u8path(arguments[3])).lexically_normal();
    imagejxx::desktop::Window editor(preferences.u8string());
    imagejxx::desktop::settingsWandUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--path-filter-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("--path-filter-ui-case CASE_ID FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::pathFilterUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--path-authoring-edge-ui-case") {
    if(arguments.size()!=4)throw std::invalid_argument("--path-authoring-edge-ui-case CASE CARRIER FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[3]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::pathAuthoringEdgeUiCase(editor,arguments[1],arguments[2],"imgui_cpp",arguments[3]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--path-authoring-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("--path-authoring-ui-case active|scientific-active|saved|xy|xz|yz FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::pathAuthoringUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--path-segment-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("--path-segment-ui-case active|saved|xy|xz|yz FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::pathSegmentUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--root-path-ui-case") {
    if(arguments.size()!=3)throw std::invalid_argument("Use --root-path-ui-case ordinary|linked|workflow FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::rootPathUiCase(editor,arguments[1],"imgui_cpp",arguments[2]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--annotation-workflow-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --annotation-workflow-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::annotationWorkflowUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--planar-path-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --planar-path-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::planarPathUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--planar-annotation-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --planar-annotation-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::planarAnnotationUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--planar-annotation-rebind-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --planar-annotation-rebind-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::planarAnnotationRebindUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--planar-annotation-tools-ui-case") {
    if(arguments.size()!=2)throw std::invalid_argument("Use --planar-annotation-tools-ui-case FRESH_DIRECTORY");
    const auto output=std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
    imagejxx::desktop::Window editor((output.parent_path()/"preferences.json").u8string());
    imagejxx::desktop::planarAnnotationToolsUiCase(editor,"imgui_cpp",arguments[1]);return 0;
  }
  if (!arguments.empty() && arguments[0] == "--manual-threshold-ui-case") {
    if (arguments.size() != 3)
      throw std::invalid_argument("Use --manual-threshold-ui-case T01..T10 FRESH_DIRECTORY");
    const auto output = std::filesystem::absolute(std::filesystem::u8path(arguments[2]));
    imagejxx::desktop::Window editor((output.parent_path() / "preferences.json").u8string());
    imagejxx::desktop::manualThresholdUiCase(editor, arguments[1], "imgui_cpp", arguments[2]);
    return 0;
  }
  std::string smokeDir, consumerDir, keyboardDir, roundtrip, output, screenshot;
  bool smoke = false, render = false, hidden = false, binaryConsumer = false;
  bool padEdgesConsumer = false;
  bool orthogonalKeyboard = false;
  bool xyzNumericZoom = false;
  std::string numericZoomDir;
  bool gaussianHost = false;
  bool gaussianCancellation = false;
  bool gaussianResource = false;
  bool gaussianRoi = false;
  bool gaussianControls = false;
  bool gaussianReplay = false;
  bool gaussianSigma = false;
  bool gaussianPreview = false;
  bool gaussianStack = false;
  bool gaussianJobCancel = false;
  bool gaussianNonfinite = false;
  bool headlessSession = false;
  bool roiBooleanUi = false;
  bool axisPlaneExportUi = false;
  bool axisRangeResliceUi = false;
  bool axisRangeOrientationUi = false;
  bool axisRangeSlicerResizeUi = false;
  bool axisRangeSpacingUi = false;
  int frames = 0;
  std::string preferencesPath, startupObservation;
  std::optional<std::string> workspacePath;
  bool noRestoreWorkspace = false, resetLayout = false;
  bool preferencesSpecified = false, startupObservationSpecified = false;
  std::vector<std::string> paths;
  for (size_t i = 0; i < arguments.size(); ++i) {
    const auto &a = arguments[i];
    auto value = [&] {
      if (++i >= arguments.size())
        throw std::invalid_argument("Missing value for " + a);
      return arguments[i];
    };
    if (a == "--help" || a == "-h") {
      std::cout
          << "imagejxx: C++ core with Dear ImGui desktop\nUsage: imagejxx "
             "[images...] [--smoke-test --smoke-dir PATH]\n  --render-smoke    "
             "Also supports --preferences PATH --workspace PATH --no-restore-workspace --reset-layout\n  "
             "Also render a hidden OpenGL window and screenshot\n  "
             "--binary-iterations-consumer-test --consumer-dir PATH\n  "
             "--binary-pad-edges-consumer-test --consumer-dir PATH\n  "
             "--orthogonal-keyboard-test --keyboard-dir PATH\n  "
             "--xyz-numeric-zoom-consumer-test --numeric-zoom-dir PATH\n  "
             "--gaussian-host-consumer-test --consumer-dir PATH\n  "
             "--gaussian-cancellation-consumer-test --consumer-dir PATH\n  "
             "--gaussian-resource-consumer-test --consumer-dir PATH\n  "
             "--gaussian-roi-consumer-test --consumer-dir PATH\n  "
             "--gaussian-controls-consumer-test --consumer-dir PATH\n  "
             "--gaussian-replay-consumer-test --consumer-dir PATH\n  "
             "--gaussian-sigma-consumer-test --consumer-dir PATH\n  "
             "--gaussian-preview-consumer-test --consumer-dir PATH\n  "
             "--gaussian-stack-consumer-test --consumer-dir PATH\n  "
             "--gaussian-jobcancel-consumer-test --consumer-dir PATH\n  "
             "--gaussian-nonfinite-consumer-test --consumer-dir PATH\n  "
             "--headless-session-consumer-test --consumer-dir PATH\n  "
             "--roi-boolean-ui-consumer-test --consumer-dir PATH\n  "
             "--axis-plane-export-ui-consumer-test --consumer-dir PATH\n  "
             "--axis-range-reslice-ui-consumer-test --consumer-dir PATH\n  "
             "--axis-range-orientation-ui-consumer-test --consumer-dir PATH\n  "
             "--axis-range-slicer-resize-ui-consumer-test --consumer-dir PATH\n  "
             "--axis-range-spacing-ui-consumer-test --consumer-dir PATH\n  "
             "--scientific-navigation-ui-case N01..N06 FRESH_DIRECTORY\n  "
             "--scientific-orientation-ui-case O01..O12 FRESH_DIRECTORY\n  "
             "--auto-contrast-ui-case U01..U08 FRESH_DIRECTORY\n  "
             "--scientific-operation-targets-ui-case CASE_ID FRESH_DIRECTORY\n  "
             "--scientific-operation-targets-ui-test FRESH_DIRECTORY\n  "
             "--roundtrip-session INPUT --session-output OUTPUT\n  --hidden "
             "--frames N --screenshot PATH    Render N frames for validation\n";
      return 0;
    }
    if (a == "--version") {
      std::cout << "0.2.0\n";
      return 0;
    }
    if (a == "--preferences") {
      if (preferencesSpecified) throw std::invalid_argument("Duplicate --preferences");
      preferencesSpecified = true;
      preferencesPath = value();
      if (preferencesPath.empty()) throw std::invalid_argument("--preferences requires a nonempty path");
    } else if (a == "--workspace") {
      if (workspacePath) throw std::invalid_argument("Duplicate --workspace");
      workspacePath = value();
    } else if (a == "--no-restore-workspace") {
      if (noRestoreWorkspace) throw std::invalid_argument("Duplicate --no-restore-workspace");
      noRestoreWorkspace = true;
    } else if (a == "--reset-layout") {
      if (resetLayout) throw std::invalid_argument("Duplicate --reset-layout");
      resetLayout = true;
    } else if (a == "--startup-observe") {
      if (startupObservationSpecified) throw std::invalid_argument("Duplicate --startup-observe");
      startupObservationSpecified = true;
      startupObservation = value();
      if (startupObservation.empty()) throw std::invalid_argument("--startup-observe requires a path");
    } else if (a == "--smoke-test")
      smoke = true;
    else if (a == "--binary-iterations-consumer-test")
      binaryConsumer = true;
    else if (a == "--binary-pad-edges-consumer-test")
      padEdgesConsumer = true;
    else if (a == "--orthogonal-keyboard-test")
      orthogonalKeyboard = true;
    else if (a == "--xyz-numeric-zoom-consumer-test")
      xyzNumericZoom = true;
    else if (a == "--gaussian-host-consumer-test")
      gaussianHost = true;
    else if (a == "--gaussian-cancellation-consumer-test")
      gaussianCancellation = true;
    else if (a == "--gaussian-resource-consumer-test")
      gaussianResource = true;
    else if (a == "--gaussian-roi-consumer-test")
      gaussianRoi = true;
    else if (a == "--gaussian-controls-consumer-test")
      gaussianControls = true;
    else if (a == "--gaussian-replay-consumer-test")
      gaussianReplay = true;
    else if (a == "--gaussian-sigma-consumer-test")
      gaussianSigma = true;
    else if (a == "--gaussian-preview-consumer-test")
      gaussianPreview = true;
    else if (a == "--gaussian-stack-consumer-test")
      gaussianStack = true;
    else if (a == "--gaussian-jobcancel-consumer-test")
      gaussianJobCancel = true;
    else if (a == "--gaussian-nonfinite-consumer-test")
      gaussianNonfinite = true;
    else if (a == "--headless-session-consumer-test")
      headlessSession = true;
    else if (a == "--roi-boolean-ui-consumer-test")
      roiBooleanUi = true;
    else if (a == "--axis-plane-export-ui-consumer-test")
      axisPlaneExportUi = true;
    else if (a == "--axis-range-reslice-ui-consumer-test")
      axisRangeResliceUi = true;
    else if (a == "--axis-range-orientation-ui-consumer-test")
      axisRangeOrientationUi = true;
    else if (a == "--axis-range-slicer-resize-ui-consumer-test")
      axisRangeSlicerResizeUi = true;
    else if (a == "--axis-range-spacing-ui-consumer-test")
      axisRangeSpacingUi = true;
    else if (a == "--keyboard-dir")
      keyboardDir = value();
    else if (a == "--numeric-zoom-dir")
      numericZoomDir = value();
    else if (a == "--consumer-dir")
      consumerDir = value();
    else if (a == "--render-smoke") {
      render = true;
      smoke = true;
    } else if (a == "--smoke-dir")
      smokeDir = value();
    else if (a == "--roundtrip-session")
      roundtrip = value();
    else if (a == "--session-output")
      output = value();
    else if (a == "--hidden")
      hidden = true;
    else if (a == "--frames")
      frames = std::stoi(value());
    else if (a == "--screenshot")
      screenshot = value();
    else if (a.rfind("--", 0) == 0)
      throw std::invalid_argument("Unknown option: " + a);
    else
      paths.push_back(a);
  }
  const bool startupArguments = preferencesSpecified || workspacePath ||
      noRestoreWorkspace || resetLayout || startupObservationSpecified;
  const bool fixtureMode = xyzNumericZoom || orthogonalKeyboard || binaryConsumer ||
      padEdgesConsumer || gaussianHost || gaussianCancellation || gaussianResource ||
      gaussianRoi || gaussianControls || gaussianReplay || gaussianSigma || gaussianPreview ||
      gaussianStack || gaussianJobCancel || gaussianNonfinite || headlessSession ||
      roiBooleanUi || axisPlaneExportUi || axisRangeResliceUi || axisRangeOrientationUi ||
      axisRangeSlicerResizeUi || axisRangeSpacingUi || smoke || !roundtrip.empty();
  if (startupArguments && fixtureMode)
    throw std::invalid_argument("Normal startup arguments cannot be combined with fixture or roundtrip modes");
  if (workspacePath && (!paths.empty() || noRestoreWorkspace))
    throw std::invalid_argument("--workspace cannot be combined with images or --no-restore-workspace");
  if (startupObservationSpecified && (!hidden || frames <= 0 || frames > 180))
    throw std::invalid_argument("--startup-observe requires --hidden and --frames 1..180");
  if (startupObservationSpecified && std::filesystem::exists(std::filesystem::u8path(startupObservation)))
    throw std::invalid_argument("Startup observation output must be fresh");
  if (xyzNumericZoom) {
    if (numericZoomDir.empty() || orthogonalKeyboard || gaussianHost ||
        gaussianCancellation || gaussianResource || gaussianRoi ||
        gaussianControls || gaussianReplay || gaussianSigma || gaussianPreview ||
        gaussianStack || gaussianJobCancel || gaussianNonfinite ||
        headlessSession || roiBooleanUi || axisPlaneExportUi || axisRangeResliceUi || axisRangeOrientationUi || axisRangeSlicerResizeUi || axisRangeSpacingUi || binaryConsumer || padEdgesConsumer ||
        !keyboardDir.empty() || !consumerDir.empty() || smoke || !smokeDir.empty() ||
        !roundtrip.empty() || !output.empty() || hidden || frames != 0 ||
        !screenshot.empty() || !paths.empty())
      throw std::invalid_argument(
          "--xyz-numeric-zoom-consumer-test requires only --numeric-zoom-dir PATH");
    imagejxx::desktop::Window editor(
        (std::filesystem::u8path(numericZoomDir) / "preferences.json").u8string());
    return editor.xyzNumericZoomConsumerTest(numericZoomDir);
  }
  if (!numericZoomDir.empty())
    throw std::invalid_argument(
        "--numeric-zoom-dir requires --xyz-numeric-zoom-consumer-test");
  if (orthogonalKeyboard) {
    if (keyboardDir.empty() || gaussianHost || gaussianCancellation || gaussianResource || gaussianRoi || gaussianControls || gaussianReplay || gaussianSigma || gaussianPreview || gaussianStack || gaussianJobCancel || gaussianNonfinite || headlessSession || roiBooleanUi || axisPlaneExportUi || axisRangeResliceUi || axisRangeOrientationUi || axisRangeSlicerResizeUi || axisRangeSpacingUi || binaryConsumer || padEdgesConsumer || !consumerDir.empty() ||
        smoke || !smokeDir.empty() || !roundtrip.empty() || !output.empty() ||
        hidden || frames != 0 || !screenshot.empty() || !paths.empty())
      throw std::invalid_argument(
          "--orthogonal-keyboard-test requires only --keyboard-dir PATH");
    imagejxx::desktop::Window editor(
        (std::filesystem::u8path(keyboardDir) / "preferences.json").u8string());
    return editor.orthogonalKeyboardTest(keyboardDir);
  }
  if (!keyboardDir.empty())
    throw std::invalid_argument("--keyboard-dir requires --orthogonal-keyboard-test");
  if (binaryConsumer || padEdgesConsumer || gaussianHost || gaussianCancellation || gaussianResource || gaussianRoi || gaussianControls || gaussianReplay || gaussianSigma || gaussianPreview || gaussianStack || gaussianJobCancel || gaussianNonfinite || headlessSession || roiBooleanUi || axisPlaneExportUi || axisRangeResliceUi || axisRangeOrientationUi || axisRangeSlicerResizeUi || axisRangeSpacingUi) {
    if ((int(binaryConsumer) + int(padEdgesConsumer) + int(gaussianHost) + int(gaussianCancellation) + int(gaussianResource) + int(gaussianRoi) + int(gaussianControls) + int(gaussianReplay) + int(gaussianSigma) + int(gaussianPreview) + int(gaussianStack) + int(gaussianJobCancel) + int(gaussianNonfinite) + int(headlessSession) + int(roiBooleanUi) + int(axisPlaneExportUi) + int(axisRangeResliceUi) + int(axisRangeOrientationUi) + int(axisRangeSlicerResizeUi) + int(axisRangeSpacingUi) != 1) || consumerDir.empty() || smoke || !smokeDir.empty() ||
        !roundtrip.empty() || !output.empty() || hidden ||
        frames != 0 || !screenshot.empty() || !paths.empty())
      throw std::invalid_argument(
          "A single consumer test requires only --consumer-dir PATH");
    imagejxx::desktop::Window editor(
        (std::filesystem::u8path(consumerDir) / "preferences.json").u8string());
    if (gaussianHost)
      return editor.gaussianHostConsumerTest(consumerDir);
    if (headlessSession)
      return editor.headlessSessionConsumerTest(consumerDir);
    if (roiBooleanUi)
      return editor.roiBooleanUiConsumerTest(consumerDir);
    if (axisRangeOrientationUi)
      return editor.axisRangeOrientationUiConsumerTest(consumerDir);
    if (axisRangeSlicerResizeUi)
      return imagejxx::desktop::axisRangeSlicerResizeUiConsumerTest(editor, consumerDir);
    if (axisRangeSpacingUi)
      return imagejxx::desktop::axisRangeSpacingUiConsumerTest(editor, consumerDir);
    if (axisRangeResliceUi)
      return editor.axisRangeResliceUiConsumerTest(consumerDir);
    if (axisPlaneExportUi)
      return editor.axisPlaneExportUiConsumerTest(consumerDir);
    if (gaussianCancellation)
      return editor.gaussianCancellationConsumerTest(consumerDir);
    if (gaussianResource)
      return editor.gaussianResourceConsumerTest(consumerDir);
    if (gaussianRoi)
      return editor.gaussianRoiConsumerTest(consumerDir);
    if (gaussianControls)
      return editor.gaussianControlsConsumerTest(consumerDir);
    if (gaussianReplay)
      return editor.gaussianReplayConsumerTest(consumerDir);
    if (gaussianSigma)
      return editor.gaussianSigmaConsumerTest(consumerDir);
    if (gaussianPreview)
      return editor.gaussianPreviewConsumerTest(consumerDir);
    if (gaussianStack)
      return editor.gaussianStackConsumerTest(consumerDir);
    if (gaussianJobCancel)
      return editor.gaussianJobCancelConsumerTest(consumerDir);
    if (gaussianNonfinite)
      return editor.gaussianNonfiniteConsumerTest(consumerDir);
    return padEdgesConsumer ? editor.binaryPadEdgesConsumerTest(consumerDir)
                            : editor.binaryIterationsConsumerTest(consumerDir);
  }
  if (!consumerDir.empty())
    throw std::invalid_argument(
        "--consumer-dir requires a consumer test");
  if (!roundtrip.empty()) {
    if (output.empty())
      throw std::invalid_argument(
          "--roundtrip-session requires --session-output");
    imagejxx::desktop::Json results, workflow;
    auto docs = imagejxx::desktop::loadSession(roundtrip, results, workflow);
    imagejxx::desktop::saveSession(output, docs, results, workflow);
    return 0;
  }
  if (smoke) {
    if (smokeDir.empty())
      smokeDir =
          (std::filesystem::temp_directory_path() /
           ("imagejxx-smoke-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count())))
              .u8string();
    std::filesystem::create_directories(std::filesystem::u8path(smokeDir));
  }
  imagejxx::desktop::Window editor(smoke ? smokeDir + "/preferences.json" : preferencesPath);
  if (smoke) {
    int result = editor.smokeTest(smokeDir);
    if (render)
      result = editor.run({true, 8, smokeDir + "/native-desktop.png"});
    return result;
  }
  imagejxx::StartupRequest request;
  request.explicit_paths = std::move(paths);
  request.explicit_workspace = std::move(workspacePath);
  request.restore_workspace = noRestoreWorkspace ? imagejxx::StartupOverride::Suppress
                                                : imagejxx::StartupOverride::Inherit;
  request.reset_layout = resetLayout;
  editor.startStartup(request);
  const auto result = editor.run({hidden, frames, screenshot});
  if (startupObservationSpecified)
    imagejxx::desktop::writeJson(startupObservation, editor.startupObservation("cpp"));
  return result;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
  try {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
      int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr,
                                  nullptr);
      std::string s(n, '\0');
      WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr,
                          nullptr);
      s.pop_back();
      args.push_back(std::move(s));
    }
    return run(args);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
#else
int main(int argc, char **argv) {
  try {
    return run({argv + 1, argv + argc});
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
#endif
