#include "shadow_ui_probe.hpp"
#include "histogram_workflow_ui_probe.hpp"
#include "animation_ui_probe.hpp"
#include "saved_roi_budget_ui_probe.hpp"
#include "rounded_legacy_ui_probe.hpp"
#include "rounded_roi_import_ui_probe.hpp"
#include "rounded_roi_export_ui_probe.hpp"
#include "ieee_viewing_ui_probe.hpp"
#include "rgb_dataset_ui_probe.hpp"
#include "../native/binding_metadata.hpp"
#include "auto_contrast_ui_probe.hpp"
#include "axis_range_slicer_resize_ui_probe.hpp"
#include "axis_spacing_ui_probe.hpp"
#include "channel_display_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "history_settings_ui_probe.hpp"
#include "manual_threshold_ui_probe.hpp"
#include "planar_annotation_matrix.hpp"
#include "planar_annotation_ui_probe.hpp"
#include "rank_runtime_probe.hpp"
#include "rank_ui_probe.hpp"
#include "roi_import_ui_probe.hpp"
#include "saved_roi_geometry_ui_probe.hpp"
#include "scientific_io_ui_probe.hpp"
#include "scientific_navigation_ui_probe.hpp"
#include "scientific_operation_targets_ui_probe.hpp"
#include "scientific_orientation_ui_probe.hpp"
#include "scientific_roi_manager_ui_probe.hpp"
#include "scientific_session_ui_probe.hpp"
#include "scientific_slicing_ui_probe.hpp"
#include "selection_creation_ui_probe.hpp"
#include "settings_io.hpp"
#include "shortcuts_ui_probe.hpp"
#include "slicer_resize_ui_probe.hpp"
#include "startup_ui_probe.hpp"
#include "window.hpp"
#include "rendering.hpp"
#include "z_subset_ui_probe.hpp"
#include <cmath>
#include <cstring>
#include <imagejxx/operations.hpp>
#include <imagejxx/channel_display_command.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;
using namespace imagejxx::desktop;
namespace {
std::string startupText(py::handle value, const char *name) {
  if (!PyUnicode_Check(value.ptr()))
    throw py::type_error(std::string(name) + " must be a string");
  Py_ssize_t size = 0;
  const auto bytes = PyUnicode_AsUTF8AndSize(value.ptr(), &size);
  if (!bytes)
    throw py::error_already_set();
  return {bytes, static_cast<std::size_t>(size)};
}
bool startupBool(py::handle value, const char *name) {
  if (!PyBool_Check(value.ptr()))
    throw py::type_error(std::string(name) + " must be a Boolean");
  return py::cast<bool>(value);
}
template <class T> py::array planeArray(const imagejxx::Image &image) {
  std::vector<py::ssize_t> shape{image.height, image.width};
  if (image.channels > 1)
    shape.push_back(image.channels);
  py::array_t<T> output(shape);
  auto destination = output.mutable_data();
  {
    py::gil_scoped_release release;
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
      destination[i] = static_cast<T>(image.pixels[i]);
  }
  return output;
}
py::array planeArray(const imagejxx::Image &image, imagejxx::FloatingSamplePolicy policy) {
  using imagejxx::PixelType;
  if (image.type == PixelType::UInt8)
    return planeArray<std::uint8_t>(image);
  if (image.type == PixelType::UInt16)
    return planeArray<std::uint16_t>(image);
  if (image.type == PixelType::Float32 && policy != imagejxx::FloatingSamplePolicy::FiniteOnly) {
    py::array_t<float> output({image.height,image.width});
    auto *destination=reinterpret_cast<unsigned char *>(output.mutable_data());
    py::gil_scoped_release release;
    for(std::size_t i=0;i<image.pixels.size();++i) {
      const auto word=imagejxx::float32_source_sample_bits(image.pixels[i],policy);
      std::memcpy(destination+i*4,&word,4);
    }
    return output;
  }
  if (image.type == PixelType::Float32)
    return planeArray<float>(image);
  return planeArray<double>(image);
}
} // namespace
namespace imagejxx::desktop {
// Only this private bridge can request language-return staging. It introduces
// no public callback hook, and Window invokes it before retaining output-array
// pointers and before native publication locks.
class SavedRoiGeometryPythonAccess {
public:
  static py::object apply(Window &window, SavedRoiGeometryRequest request) {
    window.checkThread();
    py::object reply;
    const auto stage = [&](const Json &value) {
      py::gil_scoped_acquire acquire;
      reply =
          imagejxx::bindings::metadata_python(metadataObjectFromJson(value));
    };
    {
      py::gil_scoped_release release;
      window.applySavedRoiGeometryStaged(request, stage);
    }
    return reply;
  }
  static py::object replay(Window &window, py::object record,
                           py::object completeRequest) {
    window.checkThread();
    const auto captured = imagejxx::bindings::metadata_object_value(record);
    {
      py::gil_scoped_release release;
      window.validateSavedRoiGeometryReplayRecord(captured);
    }
    std::optional<SavedRoiGeometryRequest> request;
    if (!completeRequest.is_none()) {
      if (!py::isinstance<SavedRoiGeometryRequest>(completeRequest))
        throw py::type_error(
            "Saved ROI rebind requires a complete typed request");
      request = completeRequest.cast<SavedRoiGeometryRequest>();
    }
    py::object reply;
    const auto stage = [&](const Json &value) {
      py::gil_scoped_acquire acquire;
      reply =
          imagejxx::bindings::metadata_python(metadataObjectFromJson(value));
    };
    {
      py::gil_scoped_release release;
      window.replaySavedRoiGeometryStaged(captured, std::move(request), stage);
    }
    return reply;
  }
};
} // namespace imagejxx::desktop
PYBIND11_MODULE(_desktop, m) {
  m.doc() = "Optional Dear ImGui desktop over the imagejxx C++ core";
  m.def("render_dataset_plane", [](imagejxx::Dataset source, py::dict metadata) {
    const auto shape=source.shape();
    if(shape.t!=1 || shape.c!=1 || shape.z!=1) throw py::value_error("Detached display requires one source plane");
    const auto captured=metadataJson(imagejxx::bindings::metadata_object_value(metadata));
    const auto image=[&] {
      py::gil_scoped_release release;
      return displayImage(*source.plane(),captured,source.sample_policy());
    }();
    return planeArray(image,imagejxx::FloatingSamplePolicy::FiniteOnly);
  },py::arg("source"),py::arg("metadata"));
  py::register_local_exception_translator([](std::exception_ptr error) {
    try {
      if (error)
        std::rethrow_exception(error);
    } catch (const imagejxx::ShortcutProfileError &failure) {
      auto type =
          py::module_::import("imagejxx._native").attr("ShortcutProfileError");
      auto instance = type(failure.what());
      instance.attr("diagnostics") = py::cast(failure.diagnostics());
      PyErr_SetObject(type.ptr(), instance.ptr());
    } catch (const imagejxx::StartupError &failure) {
      auto type = py::module_::import("imagejxx._native").attr("StartupError");
      auto instance = type(failure.what());
      instance.attr("diagnostic") = py::cast(failure.diagnostic());
      PyErr_SetObject(type.ptr(), instance.ptr());
    }
  });
  py::class_<GaussianCancellationSession,
             std::shared_ptr<GaussianCancellationSession>>(m,
                                                           "_H1CancelSession")
      .def(py::init<>())
      .def("wait_for_cancel_point",
           &GaussianCancellationSession::waitForCancelPoint,
           py::arg("timeout_ms") = 5000,
           py::call_guard<py::gil_scoped_release>())
      .def("cancel_entered", &GaussianCancellationSession::cancelEntered,
           py::call_guard<py::gil_scoped_release>())
      .def("cancel_returned", &GaussianCancellationSession::cancelReturned,
           py::call_guard<py::gil_scoped_release>())
      .def("abort", &GaussianCancellationSession::abort,
           py::call_guard<py::gil_scoped_release>())
      .def("diagnostics", [](const GaussianCancellationSession &session) {
        py::gil_scoped_release release;
        return session.diagnostics().dump();
      });
  py::class_<Window>(m, "Editor")
      .def("_scientific_roi_manager_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return scientificRoiManagerUiCase(window, "imgui_python",
                                               directory)
                 .dump();
           })
      .def("_z_subset_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return zSubsetUiCase(window, "imgui_python", directory).dump();
           })
      .def("_axis_spacing_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return axisSpacingUiCase(window, "imgui_python", directory).dump();
           })
      .def(
          "_slicer_resize_ui_case",
          [](Window &window, const std::string &directory) {
            py::gil_scoped_release release;
            return slicerResizeUiCase(window, "imgui_python", directory).dump();
          })
      .def("_planar_annotation_matrix_ui_case",
           [](Window &window, const std::string &id,
              const std::string &directory) {
             py::gil_scoped_release release;
             return planarAnnotationMatrixUiCase(window, id, "imgui_python",
                                                 directory)
                 .dump();
           })
      .def("_roi_import_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiImportUiCase(window, "imgui_python", directory).dump();
           })
      .def("_roi_import_limits_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiImportLimitsUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_mixed_export_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiMixedExportUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_native_workflow_batch_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return nativeWorkflowBatchUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_float32_tiff_ui_case",
           [](Window &window, const std::string &case_id, const std::string &directory, const std::string &fixture) {
             py::gil_scoped_release release;
             return float32TiffUiCase(window,case_id,"imgui_python",directory,fixture).dump();
           })
      .def("_virtual_tiff_ui_case",[](Window &w,const std::string &mode,const std::string &directory){py::gil_scoped_release release;return virtualTiffUiCase(w,mode,"imgui_python",directory).dump();})
      .def("_scientific_tiff_ui_case",
           [](Window &window, const std::string &directory,
              const std::string &fixture) {
             py::gil_scoped_release release;
             return scientificTiffUiCase(window, "imgui_python", directory,
                                         fixture)
                 .dump();
           })
      .def("_scientific_process_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return scientificProcessUiCase(window, "imgui_python", directory).dump();
           })
      .def("_histogram_workflow_ui_case",[](Window &window,const std::string &id,const std::string &directory){
        py::gil_scoped_release release;return histogramWorkflowUiCase(window,id,"imgui_python",directory).dump();
      })
      .def("_native_workflow_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return nativeWorkflowUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_bare_edit_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiBareEditUiCase(window, "imgui_python", directory).dump();
           })
      .def("_mixed_import_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiMixedImportUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_roi_import_jobs_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiImportJobsUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_roi_import_browse_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiImportBrowseUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_roi_import_queue_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiImportQueueUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_roi_import_rebind_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiImportRebindUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_animation_ui_case",
             [](Window &window,const std::string &caseId,const std::string &directory) {
               py::gil_scoped_release release;
               return animationUiCase(window,caseId,"imgui_python",directory).dump();
             },py::arg("case_id"),py::arg("directory"))
      .def("_shadow_demo_ui_case",
             [](Window &window,const std::string &caseId,const std::string &directory) {
               py::gil_scoped_release release;
               return shadowDemoUiCase(window,caseId,"imgui_python",directory).dump();
             },py::arg("case_id"),py::arg("directory"))
      .def("_shadow_preset_ui_case",
             [](Window &window,const std::string &caseId,const std::string &directory) {
               py::gil_scoped_release release;
               return shadowPresetUiCase(window,caseId,"imgui_python",directory).dump();
             },py::arg("case_id"),py::arg("directory"))
      .def("_scientific_shadow_ui_case",
             [](Window &window,const std::string &direction,const std::string &directory) {
               py::gil_scoped_release release;
               return scientificShadowUiCase(window,direction,"imgui_python",directory).dump();
             },py::arg("direction"),py::arg("directory"))
      .def("_shadow_ui_case",
             [](Window &window,const std::string &direction,const std::string &directory) {
               py::gil_scoped_release release;
               return shadowUiCase(window,direction,"imgui_python",directory).dump();
             },py::arg("direction"),py::arg("directory"))
      .def("_saved_roi_pointer_budget_ui_case",
           [](Window &window,const std::string &caseId,const std::string &directory) {
             py::gil_scoped_release release;
             return savedRoiPointerBudgetUiCase(window,caseId,"imgui_python",directory).dump();
           },py::arg("case_id"),py::arg("directory"))
      .def("_saved_roi_budget_ui_case",
           [](Window &window,const std::string &caseId,const std::string &directory) {
             py::gil_scoped_release release;
             return savedRoiBudgetUiCase(window,caseId,"imgui_python",directory).dump();
           },py::arg("case_id"),py::arg("directory"))
      .def("_rounded_legacy_ui_case",
           [](Window &window,const std::string &caseId,const std::string &directory) {
             py::gil_scoped_release release;
             return roundedLegacyUiCase(window,caseId,"imgui_python",directory).dump();
           },py::arg("case_id"),py::arg("directory"))
      .def("_rounded_roi_import_ui_case",
           [](Window &window,const std::string &caseId,const std::string &directory) {
             py::gil_scoped_release release;
             return roundedRoiImportUiCase(window,caseId,"imgui_python",directory).dump();
           },py::arg("case_id"),py::arg("directory"))
      .def("_roi_import_replacement_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiImportReplacementUiCase(window, "imgui_python",
                                               directory)
                 .dump();
           })
      .def("_rounded_roi_export_ui_case",
           [](Window &window,const std::string &caseId,const std::string &directory) {
             py::gil_scoped_release release;
             return roundedRoiExportUiCase(window,caseId,"imgui_python",directory).dump();
           },py::arg("case_id"),py::arg("directory"))
      .def("_roi_export_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return roiExportUiCase(window, "imgui_python", directory).dump();
           })
      .def("_display_annotation_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return displayAnnotationUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def(
          "_roi_zip_export_ui_case",
          [](Window &window, const std::string &directory) {
            py::gil_scoped_release release;
            return roiZipExportUiCase(window, "imgui_python", directory).dump();
          })
      .def("_planar_annotation_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return planarAnnotationUiCase(window, "imgui_python", directory)
                 .dump();
           })
      .def("_planar_annotation_rebind_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return planarAnnotationRebindUiCase(window, "imgui_python",
                                                 directory)
                 .dump();
           })
      .def("_planar_annotation_tools_ui_case",
           [](Window &window, const std::string &directory) {
             py::gil_scoped_release release;
             return planarAnnotationToolsUiCase(window, "imgui_python",
                                                directory)
                 .dump();
           })
      .def(py::init([](py::object preferencesPath, py::object images,
                       py::object workspace, py::object restore,
                       py::object reset) {
             const auto preferences =
                 startupText(preferencesPath, "preferences_path");
             imagejxx::StartupRequest request;
             if (!images.is_none()) {
               if (!PyList_Check(images.ptr()) && !PyTuple_Check(images.ptr()))
                 throw py::type_error(
                     "images must be a list or tuple of path strings");
               const auto sequence =
                   py::reinterpret_borrow<py::sequence>(images);
               request.explicit_paths.reserve(sequence.size());
               for (auto path : sequence)
                 request.explicit_paths.push_back(
                     startupText(path, "image path"));
             }
             if (!workspace.is_none())
               request.explicit_workspace = startupText(workspace, "workspace");
             if (!restore.is_none())
               request.restore_workspace =
                   startupBool(restore, "restore_workspace")
                       ? imagejxx::StartupOverride::RequireConfigured
                       : imagejxx::StartupOverride::Suppress;
             request.reset_layout = startupBool(reset, "reset_layout");
             py::gil_scoped_release release;
             auto editor = std::make_unique<Window>(preferences);
             editor->startStartup(request);
             return editor;
           }),
           py::arg("preferences_path") = "", py::kw_only(),
           py::arg("images") = py::none(), py::arg("workspace") = py::none(),
           py::arg("restore_workspace") = py::none(),
           py::arg("reset_layout") = false)
      .def("_rgb_dataset_ui_case",[](Window &window,const std::string &path){
        py::gil_scoped_release release;return rgbDatasetUiCase(window,"imgui_python",path).dump();
      })
      .def_property_readonly(
          "startup_state",
          [](const Window &window) {
            imagejxx::MetadataObject value;
            {
              py::gil_scoped_release release;
              value = metadataObjectFromJson(window.startupState());
            }
            return imagejxx::bindings::metadata_python(value);
          })
      .def("retry_startup", &Window::retryStartup,
           py::call_guard<py::gil_scoped_release>())
      .def(
          "_startup_workspace_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &directory) {
            py::gil_scoped_release release;
            return startupUiCase(window, caseId, "imgui_python", directory)
                .dump();
          },
          py::arg("case_id"), py::arg("directory"))
      .def("_startup_observation",
           [](const Window &window) {
             py::gil_scoped_release release;
             return window.startupObservation("python").dump();
           })
      .def("start_empty_startup", &Window::startEmptyStartup,
           py::call_guard<py::gil_scoped_release>())
      .def("disable_startup_restore", &Window::disableStartupRestore,
           py::call_guard<py::gil_scoped_release>())
      .def(
          "reset_layout",
          [](Window &window, py::object persist) {
            const auto save = startupBool(persist, "persist");
            py::gil_scoped_release release;
            window.resetLayout(save);
          },
          py::arg("persist") = true)
      .def(
          "_auto_contrast_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return autoContrastUiCase(window, caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_selection_creation_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return selectionCreationUiCase(window, caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_channel_display_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return channelDisplayUiCase(window, caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_scientific_operation_targets_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return scientificOperationTargetsUiCase(window, caseId, context,
                                                    directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def("_ieee_viewing_ui_case", [](Window &window,const std::string &caseId,
          const std::string &context,const std::string &directory) {
        py::gil_scoped_release release;
        return ieeeViewingUiCase(window,caseId,context,directory).dump();
      },py::arg("case_id"),py::arg("context"),py::arg("directory"))
      .def(
          "_headless_session_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.headlessSessionCase(caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_axis_range_reslice_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.axisRangeResliceUiCase(caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_manual_threshold_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return manualThresholdUiCase(window, caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_scientific_session_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return scientificSessionUiCase(window, caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_scientific_navigation_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return scientificNavigationUiCase(window, caseId, context,
                                              directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_axis_range_orientation_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.axisRangeOrientationUiCase(caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_axis_range_slicer_resize_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return axisRangeSlicerResizeUiCase(window, caseId, context,
                                               directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_axis_range_spacing_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return axisRangeSpacingUiCase(window, caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_axis_plane_export_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.axisPlaneExportUiCase(caseId, context, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_roi_boolean_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.roiBooleanUiCase(caseId, context, directory).dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_resource_case",
          [](Window &window, const std::string &context,
             const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianResourceCase(context, directory).dump();
          },
          py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_roi_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianRoiCase(variant, context, directory).dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_rank_runtime_case",
          [](Window &window, const std::string &plan,
             const std::string &directory) {
            const auto value = Json::parse(plan);
            py::gil_scoped_release release;
            return rankRuntimeCase(window, value, "imgui_python", directory)
                .dump();
          },
          py::arg("plan"), py::arg("directory"))
      .def(
          "_history_settings_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &directory) {
            py::gil_scoped_release release;
            return historySettingsUiCase(window, caseId, "imgui_python",
                                         directory)
                .dump();
          },
          py::arg("case_id"), py::arg("directory"))
      .def(
          "_scientific_orientation_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &directory) {
            py::gil_scoped_release release;
            return scientificOrientationUiCase(window, caseId, "imgui_python",
                                               directory)
                .dump();
          },
          py::arg("case_id"), py::arg("directory"))
      .def(
          "_shortcuts_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &directory) {
            py::gil_scoped_release release;
            return shortcutsUiCase(window, caseId, "imgui_python", directory)
                .dump();
          },
          py::arg("case_id"), py::arg("directory"))
      .def(
          "_saved_roi_geometry_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &directory) {
            py::gil_scoped_release release;
            return savedRoiGeometryUiCase(window, caseId, "imgui_python",
                                          directory)
                .dump();
          },
          py::arg("case_id"), py::arg("directory"))
      .def(
          "_saved_roi_geometry_ui_dependencies",
          [](Window &window, const std::string &directory) {
            py::gil_scoped_release release;
            return savedRoiGeometryUiDependencies(window, "imgui_python",
                                                  directory)
                .dump();
          },
          py::arg("directory"))
      .def(
          "_rank_ui_case",
          [](Window &window, const std::string &caseId,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return rankUiCase(window, caseId, context, directory).dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_controls_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianControlsCase(variant, context, directory)
                .dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_replay_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianReplayCase(variant, context, directory)
                .dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_sigma_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianSigmaCase(variant, context, directory).dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_preview_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianPreviewCase(variant, context, directory)
                .dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_stack_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianStackCase(variant, context, directory).dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_jobcancel_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianJobCancelCase(variant, context, directory)
                .dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_gaussian_nonfinite_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.gaussianNonfiniteCase(variant, context, directory)
                .dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_xyz_numeric_zoom_case",
          [](Window &window, const std::string &variant,
             const std::string &context, const std::string &directory) {
            py::gil_scoped_release release;
            return window.xyzNumericZoomCase(variant, context, directory)
                .dump();
          },
          py::arg("variant"), py::arg("context"), py::arg("directory"))
      .def(
          "_h1_case",
          [](Window &window, const std::string &caseId,
             const std::string &context,
             const std::shared_ptr<GaussianCancellationSession> &session,
             const std::string &directory) {
            py::gil_scoped_release release;
            return window
                .gaussianCancellationCase(caseId, context, session, directory)
                .dump();
          },
          py::arg("case_id"), py::arg("context"), py::arg("session"),
          py::arg("directory"))
      .def("settings",
           [](const Window &w) { return settingsJson(w.settings()).dump(); })
      .def("shortcut_profile", &Window::shortcutProfile)
      .def("preferences",
           [](const Window &w) { return w.preferencesJson().dump(); })
      .def(
          "update_settings",
          [](Window &w, const std::string &values, bool persist) {
            w.updateSettings(Json::parse(values), persist);
          },
          py::arg("values"), py::arg("persist") = true)
      .def(
          "add_document",
          [](Window &w, const std::string &data,
             const std::string &nativeState) {
            const auto item = Json::parse(data);
            auto d = nativeState.empty()
                         ? documentFromJson(item)
                         : documentFromJson(item, Json::parse(nativeState));
            d->checkpoint();
            w.addDocument(d);
          },
          py::arg("data"), py::arg("native_state") = "")
      .def("documents",
           [](const Window &w) {
             w.checkThread();
             Json docs = Json::array();
             for (const auto &d : w.documents())
               docs.push_back(documentJson(*d));
             return docs.dump();
           })
      .def(
          "document_info",
          [](const Window &w,
             std::uint64_t id) { return w.documentInfo(id).dump(); },
          py::arg("document_id") = 0)
      .def("document_infos",
           [](const Window &w) {
             w.checkThread();
             Json items = Json::array();
             for (const auto &d : w.documents())
               items.push_back(w.documentInfo(d->id));
             return items.dump();
           })
      .def(
          "document_snapshot",
          [](const Window &w,
             std::uint64_t
                 id) { return documentJson(*w.documentById(id)).dump(); },
          py::arg("document_id") = 0)
      .def("native_snapshot", &Window::nativeSnapshot,
           py::arg("document_id") = 0)
      .def("native_document", &Window::nativeDocument,
           py::arg("document_id") = 0)
      .def("virtual_view_cache_info",[](const Window &w){return w.virtualViewCacheInfo().dump();})
      .def("add_native_document", &Window::addNativeDocument,
           py::arg("document"))
      .def("add_virtual_document", [](Window &w,std::shared_ptr<imagejxx::Document> d,py::handle cap){
             if(!PyLong_Check(cap.ptr()) || PyBool_Check(cap.ptr()))throw py::value_error("Virtual view byte limit requires a positive integer");
             const auto bytes=PyLong_AsSize_t(cap.ptr());if(PyErr_Occurred())throw py::error_already_set();
             w.addVirtualDocument(std::move(d),bytes);
           },py::arg("document"),py::arg("max_view_bytes")=128ULL*1024*1024)
      .def("set_native_display", &Window::setNativeDisplay, py::arg("display"),
           py::arg("document_id") = 0)
      .def("begin_edit", &Window::beginEdit, py::arg("document_id") = 0,
           py::arg("dependencies") = imagejxx::Change::Data)
      .def("commit_edit", &Window::commitEdit)
      .def("close_document", &Window::closeDocument, py::arg("document_id") = 0,
           py::arg("discard") = false)
      .def(
          "plane",
          [](const Window &w, const std::string &view, int coordinate,
             std::uint64_t id) {
            auto d = w.documentById(id);
            auto axis = imagejxx::parse_plane(view);
            if (coordinate == -1) {
              d->normalizeView();
              coordinate =
                  axis == imagejxx::Plane::XY
                      ? d->slice()
                      : d->view()
                            .crosshair[axis == imagejxx::Plane::XZ ? 1 : 0];
            }
            auto pixels = [&] {
              py::gil_scoped_release release;
              return d->plane(axis, coordinate);
            }();
            return planeArray(pixels,d->samplePolicy());
          },
          py::arg("view") = "xy", py::arg("coordinate") = -1,
          py::arg("document_id") = 0)
      .def("select_document",
           [](Window &w, std::uint64_t id, const std::string &selection) {
             w.checkThread();
             if (w.busy())
               throw std::invalid_argument("Wait for the current edit");
             w.documentById(id)->select(Json::parse(selection));
           })
      .def_property(
          "active_index",
          [](const Window &w) {
            w.checkThread();
            return w.activeIndex();
          },
          [](Window &w, int i) {
            w.checkThread();
            if (i < 0 || i >= int(w.documents().size()))
              throw std::invalid_argument("Document index is out of range");
            if (w.busy())
              throw std::invalid_argument("Wait for the current edit");
            w.setActiveIndex(i);
          })
      .def_property(
          "recording",
          [](const Window &w) {
            w.checkThread();
            return w.recording;
          },
          [](Window &w, bool v) {
            w.checkThread();
            w.recording = v;
          })
      .def_property(
          "results",
          [](const Window &w) {
            w.checkThread();
            return w.results.dump();
          },
          [](Window &w, const std::string &s) {
            w.checkThread();
            auto rows = Json::parse(s);
            if (!rows.is_array())
              throw std::invalid_argument("Results must be an array");
            w.results = std::move(rows);
          })
      .def_property(
          "workflow",
          [](const Window &w) {
            w.checkThread();
            return w.workflow.dump();
          },
          [](Window &w, const std::string &s) {
            w.checkThread();
            auto steps = Json::parse(s);
            if (!steps.is_array())
              throw std::invalid_argument("Workflow must be an array");
            w.workflow = std::move(steps);
          })
      .def_property_readonly("busy", &Window::busy)
      .def_property_readonly("job_info", &Window::jobInfo)
      .def_property_readonly("jobs", &Window::jobs)
      .def("cancel_job", &Window::cancelJob, py::arg("job_id"))
      .def_property_readonly("error", &Window::error)
      .def("clear_error", &Window::clearError)
      .def("open_file", &Window::openFile,
           py::call_guard<py::gil_scoped_release>())
      .def("open_legacy_file", &Window::openLegacyFile,
           py::call_guard<py::gil_scoped_release>())
      .def(
          "apply_operation",
          [](Window &w, const std::string &id, const std::string &p, bool all,
             bool async, std::uint64_t documentId) {
            auto params = Json::parse(p);
            py::gil_scoped_release release;
            w.applyOperation(id, params, all, async, documentId);
          },
          py::arg("operation"), py::arg("parameters"),
          py::arg("all_slices") = false, py::arg("asynchronous") = true,
          py::arg("document_id") = 0)
      .def("_scientific_slicing_ui_case",
           [](Window &window, const std::string &caseId,
              const std::string &context, const std::string &directory) {
             py::gil_scoped_release release;
             return scientificSlicingUiCase(window, caseId, context, directory)
                 .dump();
           })
      .def(
          "apply_gaussian",
          [](Window &window, imagejxx::TargetRequest request,
             imagejxx::GaussianBlurCommandOptions options,
             std::uint64_t documentId) {
            // Copy mutable Python request fields while holding the GIL. Workers
            // and native resolution consume an owned capture after release.
            py::gil_scoped_release release;
            window.applyGaussian(request, options, documentId);
          },
          py::arg("target_request"), py::arg("options"),
          py::arg("document_id") = 0)
      .def(
          "apply_rank",
          [](Window &window, imagejxx::TargetRequest request,
             imagejxx::RankFilterCommandOptions options,
             std::uint64_t documentId) {
            py::gil_scoped_release release;
            window.applyRank(request, options, documentId);
          },
          py::arg("target_request"), py::arg("options"),
          py::arg("document_id") = 0)
      .def(
          "enable_channel_display",
          [](Window &window, std::uint64_t documentId) {
            window.checkThread();
            const Json parameters{{"document_id", documentId}};
            py::gil_scoped_release release;
            window.action("enable_channel_display", parameters);
          },
          py::arg("document_id") = 0)
      .def(
          "apply_channel_display",
          [](Window &window, py::object request, std::uint64_t documentId) {
            window.checkThread();
            // Only own JSON-compatible transport here. The shared Window action
            // parser owns exact request fields and native state interpretation.
            const auto captured =
                imagejxx::bindings::metadata_object_value(request);
            const Json parameters{{"request", metadataJson(captured)},
                                  {"document_id", documentId}};
            py::gil_scoped_release release;
            window.action("channel_display", parameters);
          },
          py::arg("request"), py::kw_only(), py::arg("document_id") = 0)
      .def(
          "replay_channel_display",
          [](Window &window, py::object record, py::object request,
             std::uint64_t documentId) {
            window.checkThread();
            const auto capturedRecord =
                imagejxx::bindings::metadata_object_value(record);
            // A malformed original never becomes acceptable through explicit
            // rebind, including when the replacement transport is itself
            // invalid.
            imagejxx::validate_channel_display_command_record(capturedRecord);
            Json parameters{{"record", metadataJson(capturedRecord)},
                            {"document_id", documentId}};
            if (!request.is_none())
              parameters["rebind_request"] = metadataJson(
                  imagejxx::bindings::metadata_object_value(request));
            py::gil_scoped_release release;
            window.action("replay_channel_display", parameters);
          },
          py::arg("record"), py::kw_only(),
          py::arg("rebind_request") = py::none(), py::arg("document_id") = 0)
      .def(
          "apply_selection_creation",
          [](Window &window, imagejxx::SelectionCreationRequest request,
             std::uint64_t documentId) {
            window.checkThread();
            py::gil_scoped_release release;
            window.applySelectionCreation(window.nativeSnapshot(documentId),
                                          request);
          },
          py::arg("request"), py::kw_only(), py::arg("document_id") = 0)
      .def(
          "replay_selection_creation",
          [](Window &window, py::object record,
             std::optional<imagejxx::SelectionCreationRequest> request,
             std::uint64_t documentId) {
            window.checkThread();
            const auto captured =
                imagejxx::bindings::metadata_object_value(record);
            py::gil_scoped_release release;
            window.replaySelectionCreation(captured, std::move(request),
                                           documentId);
          },
          py::arg("record"), py::kw_only(),
          py::arg("rebind_request") = py::none(), py::arg("document_id") = 0)
      .def("apply_saved_roi_geometry", &SavedRoiGeometryPythonAccess::apply,
           py::arg("request"))
      .def("replay_saved_roi_geometry", &SavedRoiGeometryPythonAccess::replay,
           py::arg("record"), py::kw_only(),
           py::arg("complete_rebind_request") = py::none())
      .def(
          "replay_axis_orientation",
          [](Window &window, py::object record, py::object request,
             std::uint64_t documentId) {
            auto capturedRecord =
                imagejxx::bindings::metadata_object_value(record);
            imagejxx::validate_axis_orientation_command_record(capturedRecord);
            std::optional<imagejxx::AxisRangeOrientationRequest>
                capturedRequest;
            if (!request.is_none()) {
              try {
                capturedRequest =
                    request.cast<imagejxx::AxisRangeOrientationRequest>();
              } catch (const py::cast_error &) {
                throw py::type_error(
                    "Rebind requires a complete typed orientation request");
              }
            }
            py::gil_scoped_release release;
            return window.replayAxisOrientation(capturedRecord, capturedRequest,
                                                documentId);
          },
          py::arg("record"), py::kw_only(),
          py::arg("rebind_request") = py::none(), py::arg("document_id") = 0)
      .def(
          "replay_axis_extraction",
          [](Window &window, py::object record, py::object request,
             std::uint64_t documentId) {
            auto capturedRecord =
                imagejxx::bindings::metadata_object_value(record);
            imagejxx::validate_axis_extraction_command_record(capturedRecord);
            std::optional<imagejxx::AxisExtractionRequest> capturedRequest;
            if (!request.is_none()) {
              try {
                capturedRequest =
                    request.cast<imagejxx::AxisExtractionRequest>();
              } catch (const py::cast_error &) {
                throw py::type_error(
                    "Rebind requires a complete typed extraction request");
              }
            }
            py::gil_scoped_release release;
            return window.replayAxisExtraction(capturedRecord, capturedRequest,
                                               documentId);
          },
          py::arg("record"), py::kw_only(),
          py::arg("rebind_request") = py::none(), py::arg("document_id") = 0)
      .def(
          "action",
          [](Window &w, const std::string &name, const std::string &p) {
            auto params = Json::parse(p);
            py::gil_scoped_release release;
            w.action(name, params);
          },
          py::arg("name"), py::arg("parameters") = "{}")
      .def("select",
           [](Window &w, const std::string &s) {
             if (w.busy())
               throw std::invalid_argument("Wait for the current edit");
             w.document()->select(Json::parse(s));
           })
      .def("poll", &Window::poll, py::call_guard<py::gil_scoped_release>())
      .def("wait", &Window::wait, py::call_guard<py::gil_scoped_release>())
      .def("cancel", &Window::cancel)
      .def(
          "run",
          [](Window &w, bool hidden, int frames,
             const std::string &screenshot) {
            py::gil_scoped_release release;
            return w.run({hidden, frames, screenshot});
          },
          py::arg("hidden") = false, py::arg("frames") = 0,
          py::arg("screenshot") = "")
      .def(
          "render_headless",
          [](Window &w, int frames, bool exercise, bool camera,
             const std::string &events, bool preferences, bool orthogonalBusy) {
            py::gil_scoped_release release;
            return w
                .renderHeadless(frames, exercise, camera, Json::parse(events),
                                preferences, orthogonalBusy)
                .dump();
          },
          py::arg("frames") = 3, py::arg("exercise") = false,
          py::arg("camera") = false, py::arg("events") = "[]",
          py::arg("exercise_preferences") = false,
          py::arg("exercise_orthogonal_busy") = false)
      .def("smoke_test", &Window::smokeTest,
           py::call_guard<py::gil_scoped_release>());
}
