"""ImageJ-style editing and scientific processing using C++17 kernels.

Images are NumPy arrays of shape (H, W) or (H, W, 3/4), with uint8,
uint16, float32 or float64 pixels. Stack APIs explicitly accept (Z, H, W)
or (Z, H, W, 3/4). Functions return new arrays and never mutate inputs.
"""

from __future__ import annotations

from typing import Callable, Sequence
from os import fspath as _fspath
import numpy as np
from . import _native
from ._native import Dataset as Dataset, SpatialCalibration as SpatialCalibration
from ._native import (
    DatasetReadCounts as DatasetReadCounts,
    DatasetReadObserver as DatasetReadObserver,
)
from ._native import (
    FloatingSamplePolicy as FloatingSamplePolicy,
    canonical_float32_source as canonical_float32_source,
    float32_source_sample as float32_source_sample,
    float32_source_sample_bits as float32_source_sample_bits,
)
from ._native import (
    Float32DisplayOptions as Float32DisplayOptions,
    Float32DisplayResult as Float32DisplayResult,
    Float32ThresholdDisplay as Float32ThresholdDisplay,
    render_float32_display as render_float32_display,
    float32_display_bounds as float32_display_bounds,
)
from ._native import (
    ContrastApplyOptions as ContrastApplyOptions,
    ContrastApplyLimits as ContrastApplyLimits,
    apply_contrast_imagej as apply_contrast_imagej,
    prepare_contrast_apply as prepare_contrast_apply,
    ContrastApplyCommandLimits as ContrastApplyCommandLimits,
    ContrastApplyCommandEstimate as ContrastApplyCommandEstimate,
    ResolvedContrastApplyCommand as ResolvedContrastApplyCommand,
    ContrastApplyJobEstimate as ContrastApplyJobEstimate,
    ContrastApplyJob as ContrastApplyJob,
    resolve_contrast_apply_command as resolve_contrast_apply_command,
    validate_contrast_apply_command_record as validate_contrast_apply_command_record,
    contrast_apply_command_from_record as contrast_apply_command_from_record,
    rebind_contrast_apply_command as rebind_contrast_apply_command,
    estimate_contrast_apply_command as estimate_contrast_apply_command,
    execute_contrast_apply_command as execute_contrast_apply_command,
    estimate_contrast_apply_job as estimate_contrast_apply_job,
    submit_contrast_apply as submit_contrast_apply,
)
from ._native import Settings as Settings
from ._native import auto_threshold_histogram_imagej as auto_threshold_histogram_imagej
from ._native import pooled_auto_threshold as pooled_auto_threshold, PooledThresholdResult as PooledThresholdResult
from ._native import pooled_automatic_threshold_display as pooled_automatic_threshold_display
from ._native import automatic_threshold_display as automatic_threshold_display, AutomaticThresholdDisplayResult as AutomaticThresholdDisplayResult
from ._native import PlaybackSequence as PlaybackSequence
from ._native import playback_interval_seconds as playback_interval_seconds


def write_avi(path, frames, *, fps=8, replace_existing=False,
              max_file_bytes=1024**3, max_working_bytes=512*1024**2,
              cancellation=None, progress=None, checkpoint=None):
    """Export an immutable UInt8 Z Dataset or (Z,Y,X[,3]) array as AVI.

    Arrays are copied into native owned storage before writing. RGB output is
    uncompressed BGR24; grayscale has an 8-bit gray palette. Existing files
    require explicit replacement. No source samples or document state change.
    """
    path_text = _fspath(path)
    if isinstance(path_text, bytes):
        path_text = path_text.decode("utf-8")
    if isinstance(frames, Dataset):
        data = frames
    else:
        array = np.asarray(frames)
        if array.ndim not in (3, 4):
            raise ValueError("AVI frames need shape (Z,Y,X) or (Z,Y,X,3)")
        data = Dataset(array, axes="ZYX" if array.ndim == 3 else "ZYXS")
    return _native.write_avi_dataset(path_text, data, fps=fps,
        replace_existing=replace_existing, max_file_bytes=max_file_bytes,
        max_working_bytes=max_working_bytes, cancellation=cancellation,
        progress=progress, checkpoint=checkpoint)

from ._native import (
    shortcut_registry_version as shortcut_registry_version,
    ShortcutPlatform as ShortcutPlatform,
    ShortcutKey as ShortcutKey,
    ShortcutModifiers as ShortcutModifiers,
    ShortcutScope as ShortcutScope,
    ShortcutDiagnosticKind as ShortcutDiagnosticKind,
    ShortcutEditStatus as ShortcutEditStatus,
    ShortcutFocus as ShortcutFocus,
    ShortcutDecisionReason as ShortcutDecisionReason,
    ShortcutDisposition as ShortcutDisposition,
    ShortcutChord as ShortcutChord,
    ShortcutCommand as ShortcutCommand,
    ShortcutBinding as ShortcutBinding,
    ShortcutDiagnostic as ShortcutDiagnostic,
    ShortcutProfileError as ShortcutProfileError,
    ShortcutLimits as ShortcutLimits,
    ShortcutProfile as ShortcutProfile,
    ShortcutEditResult as ShortcutEditResult,
    ShortcutEvent as ShortcutEvent,
    ShortcutContext as ShortcutContext,
    ShortcutDecision as ShortcutDecision,
    shortcut_commands as shortcut_commands,
    parse_shortcut_chord as parse_shortcut_chord,
    format_shortcut_chord as format_shortcut_chord,
    resolve_shortcut_chord as resolve_shortcut_chord,
    compile_shortcuts as compile_shortcuts,
    assign_shortcut as assign_shortcut,
    clear_shortcut as clear_shortcut,
    reset_shortcut as reset_shortcut,
    reset_shortcuts as reset_shortcuts,
    evaluate_shortcut as evaluate_shortcut,
    evaluate_shortcuts as evaluate_shortcuts,
)
from ._native import (
    PreferencesDocument as PreferencesDocument,
    PreferencesRepresentationError as PreferencesRepresentationError,
)
from ._native import (
    StartupOverride as StartupOverride,
    StartupSource as StartupSource,
    StartupStage as StartupStage,
    StartupReason as StartupReason,
    StartupDiagnostic as StartupDiagnostic,
    StartupError as StartupError,
    StartupLimits as StartupLimits,
    StartupRequest as StartupRequest,
    StartupPlan as StartupPlan,
    WorkspacePresentation as WorkspacePresentation,
    WorkspacePresentationUpdate as WorkspacePresentationUpdate,
    plan_startup as plan_startup,
    startup_stage_name as startup_stage_name,
    startup_reason_name as startup_reason_name,
    workspace_presentation as workspace_presentation,
    with_workspace_presentation as with_workspace_presentation,
    validate_preferences_layout as validate_preferences_layout,
)
from ._native import (
    RoiStyle as RoiStyle,
    ResolvedRoiStyle as ResolvedRoiStyle,
    parse_roi_color as parse_roi_color,
    roi_color_string as roi_color_string,
    roi_style as roi_style,
    with_roi_style as with_roi_style,
    resolve_roi_style as resolve_roi_style,
    render_roi_style as render_roi_style,
)
from ._native import (
    SelectionEnlargementCommandLimits as SelectionEnlargementCommandLimits,
    SelectionEnlargementUnits as SelectionEnlargementUnits,
    SelectionEnlargementGeometry as SelectionEnlargementGeometry,
    SelectionEnlargementRequest as SelectionEnlargementRequest,
    SelectionEnlargementCommandEstimate as SelectionEnlargementCommandEstimate,
    ResolvedSelectionEnlargementCommand as ResolvedSelectionEnlargementCommand,
    estimate_selection_enlargement_command as estimate_selection_enlargement_command,
    resolve_selection_enlargement_command as resolve_selection_enlargement_command,
    validate_selection_enlargement_command_record as validate_selection_enlargement_command_record,
    selection_enlargement_command_from_record as selection_enlargement_command_from_record,
    rebind_selection_enlargement_command as rebind_selection_enlargement_command,
    prepare_selection_enlargement as prepare_selection_enlargement,
)
from ._native import BinaryMorphologyOptions as BinaryMorphologyOptions
from ._native import ConversionOptions as ConversionOptions
from ._native import (
    SelectionEditor as SelectionEditor,
    SelectionRegion as SelectionRegion,
    SelectionEditLimits as SelectionEditLimits,
    SelectionEnlargementEstimate as SelectionEnlargementEstimate,
    SelectionRegionRotationEstimate as SelectionRegionRotationEstimate,
    estimate_selection_region_rotation as estimate_selection_region_rotation,
    SelectionHandle as SelectionHandle,
    SelectionHandleKind as SelectionHandleKind,
    SelectionHandleMode as SelectionHandleMode,
    SelectionFrame as SelectionFrame,
)
from ._native import FourierState as FourierState
from ._native import (
    SessionLimits as SessionLimits,
    SessionEstimate as SessionEstimate,
    SessionCapture as SessionCapture,
    DecodedSession as DecodedSession,
    estimate_session as estimate_session,
    encode_session as encode_session,
    decode_session as decode_session,
    save_session as save_session,
    load_session as load_session,
)
from ._native import (
    ManualThresholdRange as ManualThresholdRange,
    ManualThresholdLimits as ManualThresholdLimits,
    ManualThresholdEstimate as ManualThresholdEstimate,
    ManualThresholdSummary as ManualThresholdSummary,
    ManualThresholdState as ManualThresholdState,
    estimate_manual_threshold as estimate_manual_threshold,
    analyze_manual_threshold as analyze_manual_threshold,
    manual_threshold_mask as manual_threshold_mask,
    manual_threshold_selection as manual_threshold_selection,
    manual_threshold_state as manual_threshold_state,
    set_manual_threshold_state as set_manual_threshold_state,
    reset_manual_threshold_state as reset_manual_threshold_state,
    ResolvedManualThresholdCommand as ResolvedManualThresholdCommand,
    resolve_manual_threshold_command as resolve_manual_threshold_command,
    manual_threshold_command_from_record as manual_threshold_command_from_record,
    validate_manual_threshold_command_record as validate_manual_threshold_command_record,
    rebind_manual_threshold_command as rebind_manual_threshold_command,
    RegularAxisMapping as RegularAxisMapping,
    ZSubsetRequest as ZSubsetRequest,
    ResolvedZSubsetCommand as ResolvedZSubsetCommand,
    resolve_z_subset_command as resolve_z_subset_command,
    validate_z_subset_command_record as validate_z_subset_command_record,
    z_subset_command_from_record as z_subset_command_from_record,
    rebind_z_subset_command as rebind_z_subset_command,
    prepare_z_subset as prepare_z_subset,
    AxisSpacingLayout as AxisSpacingLayout,
    ResolvedAxisSpacingCommand as ResolvedAxisSpacingCommand,
    resolve_axis_spacing_command as resolve_axis_spacing_command,
    validate_axis_spacing_command_record as validate_axis_spacing_command_record,
    axis_spacing_command_from_record as axis_spacing_command_from_record,
    rebind_axis_spacing_command as rebind_axis_spacing_command,
    prepare_axis_spacing as prepare_axis_spacing,
    SlicerResizeLayoutDescriptor as SlicerResizeLayoutDescriptor,
    ResolvedSlicerResizeCommand as ResolvedSlicerResizeCommand,
    resolve_slicer_resize_command as resolve_slicer_resize_command,
    validate_slicer_resize_command_record as validate_slicer_resize_command_record,
    slicer_resize_command_from_record as slicer_resize_command_from_record,
    rebind_slicer_resize_command as rebind_slicer_resize_command,
    prepare_slicer_resize as prepare_slicer_resize,
    PlanarAnnotation as PlanarAnnotation,
    TextAnnotationPayload as TextAnnotationPayload,
    ImageAnnotationPayload as ImageAnnotationPayload,
    PlanarDisplayAnnotation as PlanarDisplayAnnotation,
    planar_display_annotations as planar_display_annotations,
    planar_display_annotation_visible as planar_display_annotation_visible,
    prepare_store_planar_display_annotation as prepare_store_planar_display_annotation,
    prepare_update_planar_display_annotation as prepare_update_planar_display_annotation,
    prepare_delete_planar_display_annotation as prepare_delete_planar_display_annotation,
    PlanarDisplayAnnotationRequest as PlanarDisplayAnnotationRequest,
    ResolvedPlanarDisplayAnnotationCommand as ResolvedPlanarDisplayAnnotationCommand,
    resolve_planar_display_annotation_command as resolve_planar_display_annotation_command,
    validate_planar_display_annotation_command_record as validate_planar_display_annotation_command_record,
    planar_display_annotation_command_from_record as planar_display_annotation_command_from_record,
    rebind_planar_display_annotation_command as rebind_planar_display_annotation_command,
    prepare_planar_display_annotation as prepare_planar_display_annotation,
    ImageJRoi as ImageJRoi,
    ImageJRoiPlaneMapping as ImageJRoiPlaneMapping,
    ImageJZPositionMapping as ImageJZPositionMapping,
    ImageJLegacyPositionMapping as ImageJLegacyPositionMapping,
    MappedImageJRoi as MappedImageJRoi,
    map_imagej_roi as map_imagej_roi,
    imagej_roi_standard_projection as imagej_roi_standard_projection,
    editable_imagej_roi_mapping as editable_imagej_roi_mapping,
    decode_imagej_roi_mapped as decode_imagej_roi_mapped,
    rebind_imagej_roi_mapping as rebind_imagej_roi_mapping,
    validate_imagej_roi_mapping as validate_imagej_roi_mapping,
    encode_imagej_roi_envelope as encode_imagej_roi_envelope,
    decode_imagej_roi_envelope as decode_imagej_roi_envelope,
    encode_imagej_roi_native_envelope as encode_imagej_roi_native_envelope,
    decode_imagej_roi_native_envelope as decode_imagej_roi_native_envelope,
    ImageJRoiImportMode as ImageJRoiImportMode,
    ImageJRoiNativeReplacement as ImageJRoiNativeReplacement,
    ImageJRoiImportItem as ImageJRoiImportItem,
    ImageJRoiImportLimits as ImageJRoiImportLimits,
    ResolvedImageJRoiImport as ResolvedImageJRoiImport,
    resolve_imagej_roi_import as resolve_imagej_roi_import,
    resolve_imagej_roi_zip_import as resolve_imagej_roi_zip_import,
    prepare_imagej_roi_import as prepare_imagej_roi_import,
    validate_imagej_roi_import_record as validate_imagej_roi_import_record,
    ImageJRoiImportRequest as ImageJRoiImportRequest,
    decode_imagej_roi_import_request as decode_imagej_roi_import_request,
    imagej_roi_import_from_record as imagej_roi_import_from_record,
    rebind_imagej_roi_import as rebind_imagej_roi_import,
    ImageJRoiSourceSummary as ImageJRoiSourceSummary,
    ImageJRoiSourceCatalog as ImageJRoiSourceCatalog,
    ResolvedImageJRoiPairing as ResolvedImageJRoiPairing,
    inspect_imagej_roi_sources as inspect_imagej_roi_sources,
    resolve_imagej_roi_pairing as resolve_imagej_roi_pairing,
    validate_imagej_roi_pairing_record as validate_imagej_roi_pairing_record,
    imagej_roi_pairing_from_record as imagej_roi_pairing_from_record,
    rebind_imagej_roi_pairing as rebind_imagej_roi_pairing,
    encode_imagej_roi_pairing_envelope as encode_imagej_roi_pairing_envelope,
    ImageJRoiExportLimits as ImageJRoiExportLimits,
    ResolvedImageJRoiExport as ResolvedImageJRoiExport,
    ImageJRoiExportRequest as ImageJRoiExportRequest,
    resolve_imagej_roi_export as resolve_imagej_roi_export,
    validate_imagej_roi_export_record as validate_imagej_roi_export_record,
    decode_imagej_roi_export_request as decode_imagej_roi_export_request,
    imagej_roi_export_from_record as imagej_roi_export_from_record,
    rebind_imagej_roi_export as rebind_imagej_roi_export,
    ImageJRoiZipExportItem as ImageJRoiZipExportItem,
    ImageJRoiZipExportLimits as ImageJRoiZipExportLimits,
    ImageJRoiZipExportMember as ImageJRoiZipExportMember,
    ResolvedImageJRoiZipExport as ResolvedImageJRoiZipExport,
    ImageJRoiZipExportRequest as ImageJRoiZipExportRequest,
    resolve_imagej_roi_zip_export as resolve_imagej_roi_zip_export,
    validate_imagej_roi_zip_export_record as validate_imagej_roi_zip_export_record,
    decode_imagej_roi_zip_export_request as decode_imagej_roi_zip_export_request,
    imagej_roi_zip_export_from_record as imagej_roi_zip_export_from_record,
    rebind_imagej_roi_zip_export as rebind_imagej_roi_zip_export,
    ImageJRoiZipEntry as ImageJRoiZipEntry,
    ImageJRoiZipLimits as ImageJRoiZipLimits,
    decode_imagej_roi_zip as decode_imagej_roi_zip,
    encode_imagej_roi_zip as encode_imagej_roi_zip,
    ImageJTextRoi as ImageJTextRoi,
    ImageJDisplayRoiMapping as ImageJDisplayRoiMapping,
    ImageJDisplayRoiLimits as ImageJDisplayRoiLimits,
    ImageJDisplayProjectionOptions as ImageJDisplayProjectionOptions,
    MappedImageJDisplayRoi as MappedImageJDisplayRoi,
    DecodedImageJDisplayRoiEnvelope as DecodedImageJDisplayRoiEnvelope,
    decode_imagej_display_roi_mapped as decode_imagej_display_roi_mapped,
    validate_imagej_display_roi_mapping as validate_imagej_display_roi_mapping,
    rebind_imagej_display_roi_mapping as rebind_imagej_display_roi_mapping,
    encode_imagej_display_roi_envelope as encode_imagej_display_roi_envelope,
    decode_imagej_display_roi_envelope as decode_imagej_display_roi_envelope,
    decode_imagej_display_roi_envelope_full as decode_imagej_display_roi_envelope_full,
    ImageJDisplayRoiNativeReplacement as ImageJDisplayRoiNativeReplacement,
    ImageJDisplayRoiImportItem as ImageJDisplayRoiImportItem,
    ImageJAnnotationImportLimits as ImageJAnnotationImportLimits,
    ResolvedImageJAnnotationImport as ResolvedImageJAnnotationImport,
    ImageJAnnotationImportRequest as ImageJAnnotationImportRequest,
    resolve_imagej_annotation_import as resolve_imagej_annotation_import,
    prepare_imagej_annotation_import as prepare_imagej_annotation_import,
    inspect_imagej_annotation_sources as inspect_imagej_annotation_sources,
    validate_imagej_annotation_import_record as validate_imagej_annotation_import_record,
    decode_imagej_annotation_import_request as decode_imagej_annotation_import_request,
    imagej_annotation_import_from_record as imagej_annotation_import_from_record,
    rebind_imagej_annotation_import as rebind_imagej_annotation_import,
    ImageJAnnotationKind as ImageJAnnotationKind,
    ImageJAnnotationSourceSummary as ImageJAnnotationSourceSummary,
    ImageJAnnotationSourceCatalog as ImageJAnnotationSourceCatalog,
    ResolvedImageJAnnotationPairing as ResolvedImageJAnnotationPairing,
    inspect_imagej_annotation_catalog as inspect_imagej_annotation_catalog,
    resolve_imagej_annotation_pairing as resolve_imagej_annotation_pairing,
    validate_imagej_annotation_pairing_record as validate_imagej_annotation_pairing_record,
    imagej_annotation_pairing_from_record as imagej_annotation_pairing_from_record,
    rebind_imagej_annotation_pairing as rebind_imagej_annotation_pairing,
    encode_imagej_annotation_pairing_envelope as encode_imagej_annotation_pairing_envelope,
    ImageJAnnotationExportItem as ImageJAnnotationExportItem,
    ImageJAnnotationExportLimits as ImageJAnnotationExportLimits,
    ImageJAnnotationExportMember as ImageJAnnotationExportMember,
    ResolvedImageJAnnotationExport as ResolvedImageJAnnotationExport,
    ImageJAnnotationExportRequest as ImageJAnnotationExportRequest,
    resolve_imagej_annotation_export as resolve_imagej_annotation_export,
    resolve_imagej_annotation_zip_export as resolve_imagej_annotation_zip_export,
    validate_imagej_annotation_export_record as validate_imagej_annotation_export_record,
    decode_imagej_annotation_export_request as decode_imagej_annotation_export_request,
    imagej_annotation_export_from_record as imagej_annotation_export_from_record,
    rebind_imagej_annotation_export as rebind_imagej_annotation_export,
    rebind_imagej_annotation_zip_export as rebind_imagej_annotation_zip_export,
    ImageJImageRoi as ImageJImageRoi,
    decode_imagej_image_roi as decode_imagej_image_roi,
    encode_imagej_image_roi as encode_imagej_image_roi,
    decode_imagej_text_roi as decode_imagej_text_roi,
    encode_imagej_text_roi as encode_imagej_text_roi,
    ImageJArrow as ImageJArrow,
    ImageJSpline as ImageJSpline,
    imagej_spline_selection as imagej_spline_selection,
    imagej_arrow_mask as imagej_arrow_mask,
    imagej_shape_mask as imagej_shape_mask,
    ImageJCompositePath as ImageJCompositePath,
    PathSegmentEdit as PathSegmentEdit,
    PathEditLimits as PathEditLimits,
    edit_composite_path as edit_composite_path,
    split_composite_path as split_composite_path,
    transform_imagej_composite_roi as transform_imagej_composite_roi,
    imagej_rounded_rectangle_mask as imagej_rounded_rectangle_mask,
    rounded_rectangle_path as rounded_rectangle_path,
    ImageJRoiLimits as ImageJRoiLimits,
    encode_imagej_roi as encode_imagej_roi,
    decode_imagej_roi as decode_imagej_roi,
    decode_imagej_composite_roi as decode_imagej_composite_roi,
    decode_imagej_composite_roi_mapped as decode_imagej_composite_roi_mapped,
    planar_annotations as planar_annotations,
    planar_annotation_voxel as planar_annotation_voxel,
    planar_annotation_world as planar_annotation_world,
    planar_annotation_boundary as planar_annotation_boundary,
    planar_annotation_visible as planar_annotation_visible,
    prepare_store_planar_annotation as prepare_store_planar_annotation,
    prepare_update_planar_annotation as prepare_update_planar_annotation,
    prepare_delete_planar_annotation as prepare_delete_planar_annotation,
    PlanarAnnotationRequest as PlanarAnnotationRequest,
    ResolvedPlanarAnnotationCommand as ResolvedPlanarAnnotationCommand,
    resolve_planar_annotation_command as resolve_planar_annotation_command,
    validate_planar_annotation_command_record as validate_planar_annotation_command_record,
    planar_annotation_command_from_record as planar_annotation_command_from_record,
    rebind_planar_annotation_command as rebind_planar_annotation_command,
    prepare_planar_annotation as prepare_planar_annotation,
)
from ._native import (
    HistogramBinning as HistogramBinning,
    DatasetHistogramRequest as DatasetHistogramRequest,
    DatasetHistogramLimits as DatasetHistogramLimits,
    DatasetHistogramEstimate as DatasetHistogramEstimate,
    DatasetHistogramResult as DatasetHistogramResult,
    estimate_dataset_histogram as estimate_dataset_histogram,
    accumulate_dataset_histogram as accumulate_dataset_histogram,
    dataset_histogram_csv as dataset_histogram_csv,
    HistogramJob as HistogramJob,
    HistogramJobEstimate as HistogramJobEstimate,
    estimate_histogram_job as estimate_histogram_job,
    submit_histogram as submit_histogram,
    HistogramCommandLimits as HistogramCommandLimits,
    ResolvedHistogramCommand as ResolvedHistogramCommand,
    resolve_histogram_command as resolve_histogram_command,
    validate_histogram_command_record as validate_histogram_command_record,
    histogram_command_from_record as histogram_command_from_record,
    rebind_histogram_command as rebind_histogram_command,
    estimate_histogram_command as estimate_histogram_command,
    execute_histogram_command as execute_histogram_command,
    TargetHistogramOptions as TargetHistogramOptions,
    TargetHistogramEstimate as TargetHistogramEstimate,
    TargetHistogramResult as TargetHistogramResult,
    estimate_target_histogram as estimate_target_histogram,
    accumulate_target_histogram as accumulate_target_histogram,
    target_histogram_csv as target_histogram_csv,
)
from ._native import (
    AutoContrastScope as AutoContrastScope,
    AutoContrastRoiPolicy as AutoContrastRoiPolicy,
    AutoContrastOutcome as AutoContrastOutcome,
    AutoContrastRequest as AutoContrastRequest,
    AutoContrastOptions as AutoContrastOptions,
    AutoContrastLimits as AutoContrastLimits,
    AutoContrastCycle as AutoContrastCycle,
    AutoContrastHistogramSummary as AutoContrastHistogramSummary,
    AutoContrastEstimate as AutoContrastEstimate,
    ResolvedAutoContrastCommand as ResolvedAutoContrastCommand,
    AutoContrastJob as AutoContrastJob,
    estimate_auto_contrast_command as estimate_auto_contrast_command,
    resolve_auto_contrast_command as resolve_auto_contrast_command,
    validate_auto_contrast_command_record as validate_auto_contrast_command_record,
    auto_contrast_command_from_record as auto_contrast_command_from_record,
    rebind_auto_contrast_command as rebind_auto_contrast_command,
)
from ._native import (
    Application as Application,
    DocumentUnavailable as DocumentUnavailable,
    HistoryInfo as HistoryInfo,
    PixelStorage as PixelStorage,
)
from ._native import (
    SavedRoiGeometryRequest as SavedRoiGeometryRequest,
    SavedRoiGeometryLimits as SavedRoiGeometryLimits,
    SavedRoiGeometryEstimate as SavedRoiGeometryEstimate,
    SavedRoiGeometryInteraction as SavedRoiGeometryInteraction,
    ResolvedSavedRoiGeometryCommand as ResolvedSavedRoiGeometryCommand,
    SavedRoiGeometryCapture as SavedRoiGeometryCapture,
    PreparedSavedRoiGeometryEdit as PreparedSavedRoiGeometryEdit,
    SavedRoiGeometryPublication as SavedRoiGeometryPublication,
    estimate_saved_roi_geometry as estimate_saved_roi_geometry,
    resolve_saved_roi_geometry as resolve_saved_roi_geometry,
    validate_saved_roi_geometry_record as validate_saved_roi_geometry_record,
    saved_roi_geometry_from_record as saved_roi_geometry_from_record,
    rebind_saved_roi_geometry as rebind_saved_roi_geometry,
)
from ._native import (
    ChannelDisplayLimits as ChannelDisplayLimits,
    ChannelDisplayEstimate as ChannelDisplayEstimate,
    ResolvedChannelDisplayCommand as ResolvedChannelDisplayCommand,
    channel_display_lut as channel_display_lut,
    validate_channel_display as validate_channel_display,
    normalize_channel_display as normalize_channel_display,
    channel_display_state as channel_display_state,
    with_channel_display as with_channel_display,
    default_channel_display as default_channel_display,
    reset_channel_display_ranges as reset_channel_display_ranges,
    effective_channel_display_range as effective_channel_display_range,
    estimate_channel_display as estimate_channel_display,
    compose_channel_display as compose_channel_display,
    resolve_channel_display_command as resolve_channel_display_command,
    validate_channel_display_command_record as validate_channel_display_command_record,
    channel_display_command_from_record as channel_display_command_from_record,
    rebind_channel_display_command as rebind_channel_display_command,
    command_display_state as command_display_state,
)
from ._native import (
    SelectionCreationOperation as SelectionCreationOperation,
    SelectionCreationGeometry as SelectionCreationGeometry,
    SelectionCreationRequest as SelectionCreationRequest,
    SelectionCreationLimits as SelectionCreationLimits,
    SelectionCreationEstimate as SelectionCreationEstimate,
    ResolvedSelectionCreationCommand as ResolvedSelectionCreationCommand,
    estimate_selection_creation as estimate_selection_creation,
    compose_selection_creation as compose_selection_creation,
    resolve_selection_creation_command as resolve_selection_creation_command,
    validate_selection_creation_command_record as validate_selection_creation_command_record,
    selection_creation_command_from_record as selection_creation_command_from_record,
    rebind_selection_creation_command as rebind_selection_creation_command,
    prepare_selection_creation as prepare_selection_creation,
    PreparedRoiManagerEdit as PreparedRoiManagerEdit,
    prepare_store_roi as prepare_store_roi,
    prepare_update_roi as prepare_update_roi,
    prepare_select_roi as prepare_select_roi,
    prepare_delete_roi as prepare_delete_roi,
    prepare_rename_roi as prepare_rename_roi,
    prepare_roi_properties as prepare_roi_properties,
    SelectionCreationBoxKind as SelectionCreationBoxKind,
    SelectionCreationBoxState as SelectionCreationBoxState,
    begin_selection_creation_box as begin_selection_creation_box,
    update_selection_creation_box as update_selection_creation_box,
    selection_creation_box_candidate as selection_creation_box_candidate,
)
from ._native import (
    Change as Change,
    Document as Document,
    DocumentSnapshot as DocumentSnapshot,
    DocumentTransaction as DocumentTransaction,
    Selection as Selection,
    ROI as ROI,
    DisplayState as DisplayState,
    Revisions as Revisions,
    RevisionConflict as RevisionConflict,
    SelectionRegionBooleanEstimate as SelectionRegionBooleanEstimate,
    estimate_selection_region_boolean as estimate_selection_region_boolean,
    combine_selection_regions as combine_selection_regions,
    estimate_roi_region_boolean as estimate_roi_region_boolean,
    prepare_roi_region_boolean as prepare_roi_region_boolean,
    combine_selections as combine_selections,
    prepare_roi_boolean as prepare_roi_boolean,
)

__version__ = _native.__version__

from ._native import (
    OperationSpec as OperationSpec,
    OperationParameter as OperationParameter,
    OperationResult as OperationResult,
    ParameterKind as ParameterKind,
    GeometryEffect as GeometryEffect,
    operation_registry as operation_registry,
    operation_spec as operation_spec,
    operation_catalog as operation_catalog,
    operation_defaults as operation_defaults,
    DefaultRule as DefaultRule,
    validate_operation as validate_operation,
    run_operation as run_operation,
    validate_operation_record as validate_operation_record,
)
from ._native import (
    AxisRange as AxisRange,
    PlanePosition as PlanePosition,
    TargetScope as TargetScope,
    RoiPolicy as RoiPolicy,
    ExecutionMode as ExecutionMode,
    OutputDestination as OutputDestination,
    TargetRequest as TargetRequest,
    ResolvedTarget as ResolvedTarget,
    resolve_target as resolve_target,
    resolve_legacy_target as resolve_legacy_target,
    target_from_record as target_from_record,
)
from ._native import (
    DatasetPolicy as DatasetPolicy,
    GeometryPolicy as GeometryPolicy,
    ExecutionOptions as ExecutionOptions,
    CancellationToken as CancellationToken,
    OperationCancelled as OperationCancelled,
    PreparedOperation as PreparedOperation,
    AxisPlaneExportRequest as AxisPlaneExportRequest,
    AxisPlaneExportResult as AxisPlaneExportResult,
    export_axis_plane as export_axis_plane,
    prepare_axis_plane_export as prepare_axis_plane_export,
    AxisRangeResliceRequest as AxisRangeResliceRequest,
    AxisRangeResliceResult as AxisRangeResliceResult,
    reslice_axis_range as reslice_axis_range,
    prepare_axis_range_reslice as prepare_axis_range_reslice,
    AxisRangeOrientationRequest as AxisRangeOrientationRequest,
    AxisRangeOrientationResult as AxisRangeOrientationResult,
    reslice_oriented_axis_range as reslice_oriented_axis_range,
    prepare_axis_range_orientation as prepare_axis_range_orientation,
    AxisRangeSampling as AxisRangeSampling,
    AxisRangeSpacingRequest as AxisRangeSpacingRequest,
    AxisRangeSpacingResult as AxisRangeSpacingResult,
    AxisRangeSpacingEstimate as AxisRangeSpacingEstimate,
    reslice_spaced_axis_range as reslice_spaced_axis_range,
    prepare_axis_range_spacing as prepare_axis_range_spacing,
    estimate_axis_range_spacing as estimate_axis_range_spacing,
    AxisRangeSlicerResizeBranch as AxisRangeSlicerResizeBranch,
    AxisRangeSlicerResizeRequest as AxisRangeSlicerResizeRequest,
    AxisRangeSlicerResizeResult as AxisRangeSlicerResizeResult,
    AxisRangeSlicerResizeEstimate as AxisRangeSlicerResizeEstimate,
    reslice_slicer_axis_range as reslice_slicer_axis_range,
    prepare_axis_range_slicer_resize as prepare_axis_range_slicer_resize,
    estimate_axis_range_slicer_resize as estimate_axis_range_slicer_resize,
    PlaneExecutionOutput as PlaneExecutionOutput,
    PublishedOperation as PublishedOperation,
    prepare_operation as prepare_operation,
)
from ._native import (
    JobState as JobState,
    JobLimits as JobLimits,
    JobOptions as JobOptions,
    JobInfo as JobInfo,
    JobUsage as JobUsage,
    JobScheduler as JobScheduler,
    OperationJob as OperationJob,
)
from ._native import (
    GaussianBlurCommandOptions as GaussianBlurCommandOptions,
    ResolvedGaussianBlurCommand as ResolvedGaussianBlurCommand,
    resolve_gaussian_command as resolve_gaussian_command,
    gaussian_command_from_record as gaussian_command_from_record,
    rebind_gaussian_command as rebind_gaussian_command,
    validate_gaussian_command_record as validate_gaussian_command_record,
    gaussian_scaled_units_available as gaussian_scaled_units_available,
    gaussian_command_region as gaussian_command_region,
)
from ._native import (
    RankFilterMethod as RankFilterMethod,
    RankFilterCommandOptions as RankFilterCommandOptions,
    ResolvedRankFilterCommand as ResolvedRankFilterCommand,
    resolve_rank_command as resolve_rank_command,
    resolve_despeckle_command as resolve_despeckle_command,
    rank_command_from_record as rank_command_from_record,
    rebind_rank_command as rebind_rank_command,
    validate_rank_command_record as validate_rank_command_record,
    rank_command_region as rank_command_region,
    rank_filter_kernel_imagej as rank_filter_kernel_imagej,
)
from ._native import (
    AxisExtractionLayout as AxisExtractionLayout,
    ResolvedAxisExtractionCommand as ResolvedAxisExtractionCommand,
    describe_axis_plane_export as describe_axis_plane_export,
    describe_axis_range_reslice as describe_axis_range_reslice,
    resolve_axis_extraction_command as resolve_axis_extraction_command,
    validate_axis_extraction_command_record as validate_axis_extraction_command_record,
    axis_extraction_command_from_record as axis_extraction_command_from_record,
    rebind_axis_extraction_command as rebind_axis_extraction_command,
    prepare_axis_extraction as prepare_axis_extraction,
)
from ._native import (
    ResolvedAxisOrientationCommand as ResolvedAxisOrientationCommand,
    describe_axis_range_orientation as describe_axis_range_orientation,
    resolve_axis_orientation_command as resolve_axis_orientation_command,
    validate_axis_orientation_command_record as validate_axis_orientation_command_record,
    axis_orientation_command_from_record as axis_orientation_command_from_record,
    rebind_axis_orientation_command as rebind_axis_orientation_command,
    prepare_axis_orientation as prepare_axis_orientation,
)


def execute_operation(operation: str, image, parameters=None):
    """Run a registered native plane operation; label count is returned when requested.

    Parameters and numerical defaults come from the installed C++ registry.
    The source array is never modified. Dataset range targeting is explicit and
    remains separate from this XY-plane entry point.
    """
    result = _native.run_operation(operation, _array(image), parameters or {})
    pixels = result.pixels
    return pixels if result.label_count is None else (pixels, result.label_count)


def wand_mask(
    image, x: int, y: int, tolerance: float = 0, connectivity: int = 4
) -> np.ndarray:
    """Select connected pixels within intensity tolerance of the seed pixel."""
    return _native.wand_mask(np.asarray(image), x, y, tolerance, connectivity)


THRESHOLD_METHODS = (
    "otsu",
    "mean",
    "isodata",
    "li",
    "triangle",
    "yen",
    "max_entropy",
    "percentile",
    "intermodes",
    "minimum",
    "moments",
    "min_error",
    "default", "ij_isodata", "huang", "huang2", "renyi_entropy", "shanbhag",
)
LOCAL_THRESHOLD_METHODS = (
    "mean",
    "median",
    "midgrey",
    "niblack",
    "sauvola",
    "bernsen",
    "phansalkar",
    "contrast",
)


def _array(image) -> np.ndarray:
    return np.asarray(image)


def _mask(mask) -> np.ndarray | None:
    if mask is None:
        return None
    array = np.asarray(mask)
    return array.astype(np.uint8) if array.dtype == np.bool_ else array


def _pair(value, name: str):
    if np.isscalar(value):
        return value, value
    values = tuple(value)
    if len(values) != 2:
        raise ValueError(f"{name} must be a scalar or a pair")
    return values


def _dtype_name(dtype) -> str:
    name = np.dtype(dtype).name
    if name not in ("uint8", "uint16", "float32", "float64"):
        raise ValueError("Output dtype must be uint8, uint16, float32 or float64")
    return name


def _conversion_options(options=None, **changes):
    if options is not None and not isinstance(options, ConversionOptions):
        raise TypeError("options must be ConversionOptions")
    base = options if options is not None else ConversionOptions()
    effective = ConversionOptions(
        base.scale,
        base.weighted_rgb,
        tuple(base.rgb_weights),
        base.range_policy,
        None if base.input_range is None else tuple(base.input_range),
        base.full_range_inversions,
        base.unsigned16_bits,
    )
    minimum, maximum = changes.pop("range_min", None), changes.pop("range_max", None)
    if (minimum is None) != (maximum is None):
        raise ValueError("Conversion range needs both minimum and maximum")
    if minimum is not None:
        effective.input_range = (minimum, maximum)
        if changes.get("range_policy") is None:
            effective.range_policy = "explicit"
    weights = list(effective.rgb_weights)
    for index, name in enumerate(("red_weight", "green_weight", "blue_weight")):
        weight = changes.pop(name, None)
        if weight is not None:
            weights[index] = weight
    effective.rgb_weights = tuple(weights)
    for name, value in changes.items():
        if value is not None:
            setattr(effective, name, value)
    return effective


def convert_image(
    image,
    dtype=np.uint8,
    *,
    options: ConversionOptions | None = None,
    scale: bool | None = None,
    weighted_rgb: bool | None = None,
    red_weight: float | None = None,
    green_weight: float | None = None,
    blue_weight: float | None = None,
    range_policy: str | None = None,
    range_min: float | None = None,
    range_max: float | None = None,
) -> np.ndarray:
    """Uncalibrated ImageJ-style grayscale conversion with explicit source range.

    uint8 RGB/RGBA uses equal or configurable weights; alpha is ignored. Scale
    uses source extrema for uint16/float -> uint8 and float -> uint16. Supplying
    a range without a policy selects explicit mode. Data mode uses per-plane
    extrema; dtype mode uses [0, type_max]. Array calls do not read UI settings.
    """
    effective = _conversion_options(
        options,
        scale=scale,
        weighted_rgb=weighted_rgb,
        red_weight=red_weight,
        green_weight=green_weight,
        blue_weight=blue_weight,
        range_policy=range_policy,
        range_min=range_min,
        range_max=range_max,
    )
    return _native.convert_image(_array(image), _dtype_name(dtype), effective)


def convert_rgb(image, *, display_range=None, lut=None) -> np.ndarray:
    """Convert one processor plane to fresh UInt8 RGB pixels in the C++ core.

    A LUT is a uint8 array of shape (256,3), default Grays. Scalar UInt8 uses
    raw palette indices unless a display range is supplied; UInt16/Float32 use
    byte conversion with that range or their observed extrema. Float64 scalar
    and RGBA input are owned extensions. UInt8 RGB/RGBA copies RGB unchanged,
    dropping alpha; range/LUT arguments are scalar-only. This does not quantize
    indexed color, merge composite C axes, or change a live document.
    """
    return _native.convert_rgb(_array(image), display_range, lut)


def invert_uint16(
    image,
    *,
    options: ConversionOptions | None = None,
    full_range_inversions: bool | None = None,
    unsigned16_bits: int | None = None,
) -> np.ndarray:
    """Invert uint16 grayscale using observed extrema or the chosen full bit range."""
    effective = _conversion_options(
        options,
        full_range_inversions=full_range_inversions,
        unsigned16_bits=unsigned16_bits,
    )
    return _native.invert_uint16(_array(image), effective)


def convert(image, dtype, *, scale: bool = False) -> np.ndarray:
    """Convert with saturating integer rounding; scale maps dtype color ranges."""
    return _native.convert(_array(image), _dtype_name(dtype), scale)


def math(image, operation: str, value: float = 0) -> np.ndarray:
    """Apply native scalar math. Integers saturate; floats keep signed results."""
    return _native.point(_array(image), operation, value)


def invert(image) -> np.ndarray:
    """Invert around 255, 65535 or 1 for uint8, uint16 or floating images."""
    return math(image, "invert")


def add(image, value: float) -> np.ndarray:
    """Add a scalar to every pixel."""
    return math(image, "add", value)


def subtract(image, value: float) -> np.ndarray:
    """Subtract a scalar from every pixel."""
    return math(image, "subtract", value)


def multiply(image, value: float) -> np.ndarray:
    """Multiply every pixel by a scalar."""
    return math(image, "multiply", value)


def divide(image, value: float) -> np.ndarray:
    """Divide pixels by a nonzero scalar."""
    return math(image, "divide", value)


def absolute(image) -> np.ndarray:
    """Take the absolute value of pixels."""
    return math(image, "abs")


def square(image) -> np.ndarray:
    """Square pixels."""
    return math(image, "square")


def sqrt(image) -> np.ndarray:
    """Take square roots of nonnegative pixels."""
    return math(image, "sqrt")


def log(image) -> np.ndarray:
    """Natural logarithm of strictly positive pixels."""
    return math(image, "log")


def log1p(image) -> np.ndarray:
    """Natural logarithm of one plus each pixel."""
    return math(image, "log1p")


def exp(image) -> np.ndarray:
    """Exponentiate pixels; non-finite results raise ValueError."""
    return math(image, "exp")


def gamma(image, value: float) -> np.ndarray:
    """Apply max_value * (pixel / max_value)**value, with value > 0."""
    return math(image, "gamma", value)


def clip(image, lower: float, upper: float) -> np.ndarray:
    """Clamp pixel values to an inclusive interval."""
    if not np.isfinite([lower, upper]).all() or lower > upper:
        raise ValueError("Clip bounds must be finite and ordered")
    return math(math(image, "maximum", lower), "minimum", upper)


def calculate(image, other, operation: str = "add", *, dtype=None) -> np.ndarray:
    """Combine same-shaped images; dtype can retain values beyond integer range.

    Operations: add, subtract, multiply, divide, minimum, maximum, average,
    difference, and, or, xor, copy. Division by a zero pixel yields zero.
    """
    return _native.calculate(
        _array(image),
        _array(other),
        operation,
        "" if dtype is None else _dtype_name(dtype),
    )


def adjust_brightness_contrast(
    image, brightness: float = 0, contrast: float = 1, *, center: float = 0
) -> np.ndarray:
    """Change actual pixels to (pixel-center)*contrast + center + brightness."""
    return _native.linear(_array(image), contrast, center * (1 - contrast) + brightness)


def normalize(
    image, lower: float = 0, upper: float | None = None, *, saturation: float = 0
) -> np.ndarray:
    """Stretch observed intensities; saturation is total percent clipped."""
    array = _array(image)
    if upper is None:
        upper = (
            255
            if array.dtype.kind == "u" and array.itemsize == 1
            else 65535
            if array.dtype.kind == "u" and array.itemsize == 2
            else 1
        )
    return _native.normalize(array, lower, upper, saturation)


def apply_lut(image, lut: Sequence[float]) -> np.ndarray:
    """Map uint8/uint16 pixels through a 256/65536-entry lookup table."""
    return _native.apply_lut(_array(image), lut)


def add_noise(
    image, amount: float, *, kind: str = "gaussian", seed: int = 0
) -> np.ndarray:
    """Gaussian stddev, uniform half-width, or salt_pepper probability.

    Seeded output is repeatable within the same C++ standard library.
    """
    return _native.add_noise(_array(image), kind, amount, seed)


def convolve(
    image, kernel, *, normalize: bool = False, border: str = "reflect", cval: float = 0
) -> np.ndarray:
    """True convolution with an odd 2D kernel; process each channel separately."""
    return _native.convolve(
        _array(image), np.asarray(kernel, dtype=np.float64), border, cval, normalize
    )


def shadow_imagej(image, direction: str = "north", *, extent=None) -> np.ndarray:
    """ImageJ directional shadow; extent is (x, y, width, height).

    UInt8 gray/RGB, UInt16 gray and finite Float32 gray are supported. Scalar
    edges replicate pixels; RGB outer borders remain unchanged.
    """
    return _native.shadow_imagej(_array(image), direction, extent)


def gaussian_blur(image, sigma=1, *, border: str = "reflect") -> np.ndarray:
    """Separable Gaussian with radius ceil(4*sigma); pairs are (sigma_y,sigma_x)."""
    sy, sx = _pair(sigma, "sigma")
    return _native.gaussian_blur(_array(image), sx, sy, border)


def gaussian_blur_imagej(image, sigma=2.0, *, extent=None) -> np.ndarray:
    """ImageJ-style Gaussian profile with nearest image edges and float stages.

    A sigma pair is (sigma_y, sigma_x). Extent is an optional (x, y, width,
    height) write rectangle in source coordinates. Pixels outside it remain
    unchanged; an irregular mask is a separate command/executor policy.
    Supports uint8 gray/RGB, uint16 gray and finite float32 gray. Returns a new
    array. The existing gaussian_blur function keeps its sigma1/reflect defaults.
    """
    sy, sx = _pair(sigma, "sigma")
    if isinstance(sx, (bool, np.bool_)) or isinstance(sy, (bool, np.bool_)):
        raise TypeError("Gaussian sigma must be numeric; Boolean values are not supported")
    return _native.gaussian_blur_imagej(_array(image), sx, sy, extent)


def smooth(image, radius: int = 1, *, border: str = "reflect") -> np.ndarray:
    """Square mean filter with an odd (2*radius+1) window."""
    return _native.box_blur(_array(image), radius, border)


ExecutionPhase = _native.ExecutionPhase

RemoveNaNsCommandOptions = _native.RemoveNaNsCommandOptions
resolve_remove_nans_command = _native.resolve_remove_nans_command
remove_nans_command_region = _native.remove_nans_command_region

TopHatCommandOptions = _native.TopHatCommandOptions
resolve_top_hat_command = _native.resolve_top_hat_command

RemoveOutliersCommandOptions = _native.RemoveOutliersCommandOptions
resolve_remove_outliers_command = _native.resolve_remove_outliers_command
remove_outliers_lut_inverted = _native.remove_outliers_lut_inverted


def remove_nans_imagej(image, radius=2.0, *, extent=None) -> np.ndarray:
    """Replace Float32 NaNs using ImageJ's NaN-aware median in C++.

    Finite values, untouched NaN payloads, and signed zeros retain their bits.
    Infinities and signaling NaNs are admitted; untouched source bits survive.
    Extent limits writes only. All-NaN neighborhoods become canonical quiet NaN.
    """
    return _native.remove_nans_imagej(_array(image), radius, extent)


def top_hat_imagej(image, radius=2.0, *, light_background=False,
                   dont_subtract=False, inverted_lut=False, extent=None) -> np.ndarray:
    """ImageJ compound Top Hat pixels, preserving finite scalar dtype.

    An optional extent is the final write rectangle; both passes read its halo.
    dont_subtract returns the opened/closed background. Display finalization
    and irregular masks belong to the editor command, not this pixel API.
    """
    return _native.top_hat_imagej(_array(image), radius, light_background,
                                dont_subtract, inverted_lut, extent)


def remove_outliers_imagej(image, radius=2.0, threshold=50.0, *, which="bright",
                          inverted_lut=False, extent=None) -> np.ndarray:
    """Remove finite scalar outliers with ImageJ's median/strict-threshold profile."""
    return _native.remove_outliers_imagej(_array(image), radius, threshold, which,
                                         inverted_lut, extent)


def despeckle_imagej(image, *, extent=None) -> np.ndarray:
    """Fixed nine-point median Despeckle in C++; preserves finite scalar dtype."""
    return _native.despeckle_imagej(_array(image), extent)


def rank_filter_imagej(image, radius=2.0, *, method="median", extent=None) -> np.ndarray:
    """ImageJ 1.54r circular rank filter with type-preserving scalar variance.

    Supports finite uint8, uint16 and float32 grayscale arrays. Radius is
    fractional, defaults to 2, and zero selects a five-point cross. Extent is
    an optional (x, y, width, height) write rectangle; the original source halo
    remains available and samples outside it stay unchanged. Irregular masks
    belong to the captured command/executor. Returns a new array.
    Variance retains uint8/uint16/float32; nonfinite float32 output raises.
    This pixel API does not apply the editor plugin final contrast stretch.
    Editor variance commands pair the final active channel/display range and
    conditional threshold reset with pixels; unknown display schemas refuse.
    """
    return _native.rank_filter_imagej(_array(image), method, radius, extent)


def rank_filter(
    image,
    radius: int = 1,
    *,
    method: str = "median",
    footprint: str = "disk",
    border: str = "reflect",
    cval: float = 0,
) -> np.ndarray:
    """Minimum, maximum, median, mean, variance or stddev in a disk/square."""
    if footprint not in ("disk", "square"):
        raise ValueError("Footprint must be disk or square")
    return _native.rank_filter(
        _array(image), radius, method, footprint == "disk", border, cval
    )


def median_filter(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Median filter; see rank_filter for footprint and border options."""
    return rank_filter(image, radius, method="median", **kwargs)


def minimum_filter(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Local minimum filter."""
    return rank_filter(image, radius, method="minimum", **kwargs)


def maximum_filter(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Local maximum filter."""
    return rank_filter(image, radius, method="maximum", **kwargs)


def variance_filter(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Population variance, returned as float64."""
    return rank_filter(image, radius, method="variance", **kwargs)


def sharpen(image, *, border: str = "reflect") -> np.ndarray:
    """Apply the ImageJ-style 3x3 sharpening kernel."""
    return convolve(
        image, [[-1, -1, -1], [-1, 12, -1], [-1, -1, -1]], normalize=True, border=border
    )


def find_edges(image, *, border: str = "reflect") -> np.ndarray:
    """Sobel gradient magnitude, returned as float64."""
    return _native.sobel(_array(image), border)


def laplacian(image, *, border: str = "reflect") -> np.ndarray:
    """Four-neighbor Laplacian; float64 retains signed derivatives."""
    return convolve(
        convert(image, np.float64), [[0, 1, 0], [1, -4, 1], [0, 1, 0]], border=border
    )


def difference_of_gaussians(
    image, sigma_small: float = 1, sigma_large: float = 2
) -> np.ndarray:
    """Difference of Gaussian blurs with signed float64 output."""
    if sigma_small > sigma_large:
        raise ValueError("sigma_small must not exceed sigma_large")
    source = convert(image, np.float64)
    return calculate(
        gaussian_blur(source, sigma_small),
        gaussian_blur(source, sigma_large),
        "subtract",
    )


def unsharp_mask(image, sigma: float = 1, amount: float = 1) -> np.ndarray:
    """Add amount times the difference from the Gaussian-smoothed image."""
    return _native.unsharp_mask(_array(image), sigma, amount)


def bilateral_filter(
    image, radius: int = 3, *, sigma_spatial: float = 2, sigma_range: float = 25
) -> np.ndarray:
    """Edge-preserving filter; sigma_range uses the input's intensity units."""
    return _native.bilateral_filter(_array(image), radius, sigma_spatial, sigma_range)


def kuwahara_filter(image, radius: int = 2) -> np.ndarray:
    """Choose the lowest-variance quadrant's mean; color channels share a quadrant."""
    return _native.kuwahara_filter(_array(image), radius)


def anisotropic_diffusion(
    image, iterations: int = 10, *, kappa: float = 20, step: float = 0.2
) -> np.ndarray:
    """Four-neighbor Perona-Malik exponential diffusion, float64 output."""
    return _native.anisotropic_diffusion(_array(image), iterations, kappa, step)


def morphology(
    image,
    operation: str,
    radius: int = 1,
    *,
    footprint: str = "disk",
    border: str = "reflect",
    cval: float = 0,
) -> np.ndarray:
    """Grayscale erosion, dilation, opening, closing, gradient, top_hat or black_hat."""
    if footprint not in ("disk", "square"):
        raise ValueError("Footprint must be disk or square")
    return _native.morphology(
        _mask(image), operation, radius, footprint == "disk", border, cval
    )


def binary_morphology(
    image,
    operation: str,
    *,
    options: BinaryMorphologyOptions | None = None,
    iterations: int | None = None,
    neighbors: int | None = None,
    black_background: bool | None = None,
    pad_edges: bool | None = None,
) -> np.ndarray:
    """Eight-neighbor uint8 0/255 morphology; explicit keywords override options.

    Foreground is 255 with a black background, otherwise 0. Erosion counts
    background neighbors; dilation counts foreground neighbors. Open/close
    perform N iterations of each stage. Edge padding affects erosion only.
    This function does not read desktop preferences; pass settings.binary_options().
    """
    if options is not None and not isinstance(options, BinaryMorphologyOptions):
        raise TypeError("options must be BinaryMorphologyOptions")
    base = options if options is not None else BinaryMorphologyOptions()
    effective = BinaryMorphologyOptions(
        base.iterations if iterations is None else iterations,
        base.neighbors if neighbors is None else neighbors,
        base.black_background if black_background is None else black_background,
        base.pad_edges if pad_edges is None else pad_edges,
    )
    return _native.binary_morphology(_array(image), operation, effective)


def binary_erode(image, **kwargs) -> np.ndarray:
    """Binary erosion with explicit eight-neighbor options."""
    return binary_morphology(image, "erode", **kwargs)


def binary_dilate(image, **kwargs) -> np.ndarray:
    """Binary dilation with explicit eight-neighbor options."""
    return binary_morphology(image, "dilate", **kwargs)


def binary_open(image, **kwargs) -> np.ndarray:
    """N binary erosions followed by N dilations."""
    return binary_morphology(image, "open", **kwargs)


def binary_close(image, **kwargs) -> np.ndarray:
    """N binary dilations followed by N erosions."""
    return binary_morphology(image, "close", **kwargs)


def erode(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Grayscale erosion; set border='constant', cval=0 for a zero exterior."""
    return morphology(_mask(image), "erode", radius, **kwargs)


def dilate(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Grayscale dilation."""
    return morphology(_mask(image), "dilate", radius, **kwargs)


def opening(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Erode then dilate."""
    return morphology(_mask(image), "open", radius, **kwargs)


def closing(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Dilate then erode."""
    return morphology(_mask(image), "close", radius, **kwargs)


def top_hat(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Subtract the morphological opening."""
    return morphology(image, "top_hat", radius, **kwargs)


def black_hat(image, radius: int = 1, **kwargs) -> np.ndarray:
    """Subtract pixels from the morphological closing."""
    return morphology(image, "black_hat", radius, **kwargs)


def subtract_background(
    image, radius: int = 10, *, light_background: bool = False
) -> np.ndarray:
    """Disk opening/closing background subtraction; distinct from rolling ball."""
    return _native.subtract_background(_array(image), radius, light_background)


def rolling_ball_background(
    image, radius: float = 10, *, light_background: bool = False
) -> np.ndarray:
    """Direct spherical non-flat opening/closing background, in raw pixel units.

    Runtime scales with radius squared. This implements the spherical model
    without ImageJ's image shrinking or sliding-paraboloid approximations.
    """
    return _native.rolling_ball_background(_array(image), radius, light_background)


def subtract_rolling_ball(
    image, radius: float = 10, *, light_background: bool = False
) -> np.ndarray:
    """Subtract the spherical background; light-background output is background-image."""
    return _native.subtract_rolling_ball(_array(image), radius, light_background)


def remove_outliers(
    image, radius: int = 2, threshold: float = 50, *, bright: bool = True
) -> np.ndarray:
    """Replace bright/dark pixels differing from the local disk median by threshold."""
    return _native.remove_outliers(_array(image), radius, threshold, bright)


def clahe(image, *, tile_size=64, clip_limit: float = 2) -> np.ndarray:
    """Tiled, bilinearly blended CLAHE for grayscale; tile pairs are (H,W)."""
    th, tw = _pair(tile_size, "tile_size")
    return _native.clahe(_array(image), tw, th, clip_limit)


def crop(image, x: int, y: int, width: int, height: int) -> np.ndarray:
    """Copy an in-bounds rectangle in (x,y,width,height) coordinates."""
    return _native.crop(_array(image), x, y, width, height)


def paste(image, source, x: int = 0, y: int = 0, *, mask=None) -> np.ndarray:
    """Paste a source, clipping to the destination; mask has the source's shape."""
    return _native.paste(_array(image), _array(source), x, y, _mask(mask))


def fill(image, value: float, *, mask=None) -> np.ndarray:
    """Fill all pixels, or only pixels selected by a nonzero 2D mask."""
    return _native.fill(_array(image), value, _mask(mask))


def flip(image, *, horizontal: bool = False, vertical: bool = False) -> np.ndarray:
    """Flip around horizontal and/or vertical axes."""
    return _native.flip(_array(image), horizontal, vertical)


def rotate90(image, turns: int = 1) -> np.ndarray:
    """Rotate counterclockwise by turns * 90 degrees, without interpolation."""
    return _native.rotate90(_array(image), turns)


def resize(
    image, width: int, height: int, *, interpolation: str = "linear"
) -> np.ndarray:
    """Resize with pixel-center nearest, bilinear or Catmull-Rom bicubic sampling.

    This resampler does not apply a downsampling antialias filter; use binning
    for area aggregation or preblur before reducing resolution.
    """
    return _native.resize(_array(image), width, height, interpolation)


def affine_transform(
    image,
    inverse_matrix,
    *,
    output_shape=None,
    interpolation: str = "linear",
    border: str = "constant",
    cval: float = 0,
) -> np.ndarray:
    """Inverse-map output (x,y) to source with a 2x3 matrix; shape is (H,W)."""
    source = _array(image)
    matrix = np.asarray(inverse_matrix, dtype=np.float64)
    if matrix.shape not in ((2, 3), (6,)):
        raise ValueError("Inverse matrix must have shape (2,3) or (6,)")
    h, w = (
        source.shape[:2]
        if output_shape is None
        else _pair(output_shape, "output_shape")
    )
    return _native.affine_transform(
        source, matrix.ravel(), w, h, interpolation, border, cval
    )


def translate(
    image, dx: float, dy: float, *, interpolation: str = "linear", cval: float = 0
) -> np.ndarray:
    """Move pixels right by dx and down by dy."""
    return affine_transform(
        image, [[1, 0, -dx], [0, 1, -dy]], interpolation=interpolation, cval=cval
    )


def rotate(
    image,
    degrees: float,
    *,
    expand: bool = False,
    interpolation: str = "linear",
    cval: float = 0,
) -> np.ndarray:
    """Rotate counterclockwise about the image center."""
    return _native.rotate(_array(image), degrees, expand, interpolation, cval)


def binning(image, factor=2, *, method: str = "mean") -> np.ndarray:
    """Aggregate rectangular bins, including partial edge bins; pairs are (Y,X)."""
    fy, fx = _pair(factor, "factor")
    return _native.bin(_array(image), fx, fy, method)


def resize_canvas(
    image, width: int, height: int, *, offset=(0, 0), value: float = 0
) -> np.ndarray:
    """Change canvas size and paste the original at offset (x,y)."""
    x, y = _pair(offset, "offset")
    return _native.resize_canvas(_array(image), width, height, x, y, value)


def draw_line(
    image, start, end, value: float = 255, *, thickness: float = 1
) -> np.ndarray:
    """Draw a solid line between (x,y) pixel-center coordinates."""
    x1, y1 = _pair(start, "start")
    x2, y2 = _pair(end, "end")
    return _native.draw_line(_array(image), x1, y1, x2, y2, value, thickness)


def rectangle_mask(shape, x: int, y: int, width: int, height: int) -> np.ndarray:
    """Rasterize an in-bounds rectangular ROI as a uint8 0/255 mask."""
    h, w = _pair(shape, "shape")
    return _native.rectangle_mask(w, h, x, y, width, height)


def ellipse_mask(shape, center, radii) -> np.ndarray:
    """Ellipse ROI; center and radii are (x,y), pixel centers are (x+.5,y+.5)."""
    h, w = _pair(shape, "shape")
    cx, cy = _pair(center, "center")
    rx, ry = _pair(radii, "radii")
    return _native.ellipse_mask(w, h, cx, cy, rx, ry)


def polygon_mask(shape, vertices) -> np.ndarray:
    """Rasterize polygon vertices (x,y) using pixel centers and even-odd filling."""
    h, w = _pair(shape, "shape")
    return _native.polygon_mask(w, h, vertices)


def line_profile(image, start, end, *, samples: int = 0) -> np.ndarray:
    """Sample a grayscale line with bilinear interpolation and clamped endpoints."""
    x1, y1 = _pair(start, "start")
    x2, y2 = _pair(end, "end")
    return _native.line_profile(_array(image), x1, y1, x2, y2, samples)


def grayscale(image) -> np.ndarray:
    """RGB luminance with weights .299/.587/.114; ignore alpha."""
    return _native.grayscale(_array(image))


def rgb_to_hsv(image) -> np.ndarray:
    """Convert RGB to float64 H,S,V in [0,1]; floating RGB must be in [0,1]."""
    return _native.rgb_to_hsv(_array(image))


def hsv_to_rgb(image, *, dtype=np.uint8) -> np.ndarray:
    """Convert normalized H,S,V to the requested RGB dtype."""
    return _native.hsv_to_rgb(_array(image), _dtype_name(dtype))


def split_channels(image) -> list[np.ndarray]:
    """Copy interleaved color channels into separate grayscale arrays."""
    return _native.split_channels(_array(image))


def merge_channels(channels: Sequence[np.ndarray]) -> np.ndarray:
    """Merge one, three or four same-shaped, same-dtype grayscale arrays."""
    return _native.merge_channels([_array(channel) for channel in channels])


def color_deconvolution(image, stains) -> np.ndarray:
    """Return optical-density concentrations for user-provided 3x3 stain rows."""
    matrix = np.asarray(stains, dtype=np.float64)
    if matrix.shape != (3, 3):
        raise ValueError(
            "Stains must be a 3x3 matrix with one RGB absorbance vector per row"
        )
    return _native.color_deconvolution(_array(image), matrix.ravel())


def threshold(image, lower: float, upper: float | None = None) -> np.ndarray:
    """Inclusive intensity interval as a uint8 0/255 grayscale mask."""
    return _native.threshold(_array(image), lower, np.inf if upper is None else upper)


def in_range(image, lower: Sequence[float], upper: Sequence[float]) -> np.ndarray:
    """Select pixels whose channels all satisfy their inclusive range bounds."""
    return _native.in_range(_array(image), lower, upper)


def auto_threshold(image, method: str = "otsu", *, bins: int = 0,
                   ignore_black: bool = False, ignore_white: bool = False) -> float:
    """Return a histogram threshold; default uses 256 bins, including uint16."""
    return _native.auto_threshold(_array(image), method, bins, ignore_black, ignore_white)


def auto_mask(
    image, method: str = "otsu", *, bright: bool = True, bins: int = 0,
    ignore_black: bool = False, ignore_white: bool = False, stack_histogram: bool = False
) -> np.ndarray:
    """Threshold as pixel > level (bright) or pixel <= level (dark)."""
    if not isinstance(stack_histogram, bool):
        raise TypeError("stack_histogram must be a Boolean")
    return _native.auto_mask(_array(image), method, bright, bins, ignore_black, ignore_white)


def local_threshold(
    image,
    method: str = "sauvola",
    *,
    radius: int = 15,
    k: float | None = None,
    offset: float = 0,
    dynamic_range: float | None = None,
    contrast_threshold: float | None = None,
) -> np.ndarray:
    """Adaptive square-window threshold; return uint8 0/255.

    Methods are listed in LOCAL_THRESHOLD_METHODS. Float default ranges assume
    [0,1]; set dynamic_range when using floats in other intensity units.
    """
    array = _array(image)
    scale = (
        255
        if array.dtype.kind == "u" and array.itemsize == 1
        else 65535
        if array.dtype.kind == "u" and array.itemsize == 2
        else 1
    )
    if k is None:
        k = -0.2 if method == "niblack" else 0.25 if method == "phansalkar" else 0.2
    return _native.local_threshold(
        array,
        method,
        radius,
        k,
        offset,
        scale / 2 if dynamic_range is None else dynamic_range,
        scale * 15 / 255 if contrast_threshold is None else contrast_threshold,
    )


def histogram(image, bins: int = 0, *, range=None) -> tuple[np.ndarray, np.ndarray]:
    """Counts and bin edges. Defaults: 256 bins (uint8/float), 65536 (uint16)."""
    lower, upper = (np.nan, np.nan) if range is None else _pair(range, "range")
    return _native.histogram(_array(image), bins, lower, upper)


def statistics(image, *, mask=None) -> dict:
    """Count, min, max, mean, population stddev, sum, median and integrated density."""
    return _native.statistics(_array(image), _mask(mask))


def equalize_histogram(image) -> np.ndarray:
    """Global grayscale CDF equalization with first occupied bin subtraction."""
    return _native.equalize_histogram(_array(image))


def label(mask, *, connectivity: int = 8, return_num: bool = False):
    """Label nonzero foreground; background=0, labels=int32."""
    labels, count = _native.connected_components(_mask(mask), connectivity)
    return (labels, count) if return_num else labels


def region_properties(labels, *, intensity=None, spacing=(1, 1)) -> list[dict]:
    """Measure labels; spacing=(Y,X), bbox=(x,y,w,h), centroid=(y,x).

    Area and perimeter use calibrated pixel cells; centroid indices start at
    zero. Intensity fields are zero when no intensity image is supplied.
    """
    sy, sx = _pair(spacing, "spacing")
    return _native.region_properties(
        _array(labels), None if intensity is None else _array(intensity), sx, sy
    )


def analyze_particles(
    mask,
    *,
    intensity=None,
    min_area: float = 0,
    max_area: float = np.inf,
    exclude_edges: bool = False,
    connectivity: int = 8,
    spacing=(1, 1),
) -> list[dict]:
    """Connected-component measurements filtered by calibrated area and borders."""
    if min_area < 0 or max_area < min_area or np.isnan([min_area, max_area]).any():
        raise ValueError("Particle area bounds must be nonnegative and ordered")
    properties = region_properties(
        label(mask, connectivity=connectivity), intensity=intensity, spacing=spacing
    )
    return [
        p
        for p in properties
        if min_area <= p["area"] <= max_area
        and (not exclude_edges or not p["touches_border"])
    ]


def fill_holes(mask, *, connectivity: int = 4) -> np.ndarray:
    """Fill background regions disconnected from the image boundary."""
    return _native.fill_holes(_mask(mask), connectivity)


def skeletonize(mask) -> np.ndarray:
    """Zhang-Suen thinning with a zero exterior; return uint8 0/255."""
    return _native.skeletonize(_mask(mask))


def binary_outline(mask) -> np.ndarray:
    """Eight-neighbor inner boundary of binary foreground."""
    return _native.binary_outline(_mask(mask))


def remove_small_objects(
    mask, min_size: int = 64, *, connectivity: int = 8
) -> np.ndarray:
    """Keep components with at least min_size pixels."""
    if min_size < 0:
        raise ValueError("min_size must be nonnegative")
    return _native.remove_small_objects(_mask(mask), min_size, connectivity)


def clear_border(mask, *, connectivity: int = 8) -> np.ndarray:
    """Remove components touching any image edge."""
    return _native.clear_border(_mask(mask), connectivity)


def distance_transform(mask, *, spacing=(1, 1)) -> np.ndarray:
    """Exact Euclidean distance to in-image zeros; all-foreground returns infinity."""
    sy, sx = _pair(spacing, "spacing")
    return _native.distance_transform(_mask(mask), sx, sy)


def local_maxima(image, radius: int = 1) -> np.ndarray:
    """Disk-neighborhood maxima with nonconstant neighborhoods, as 0/255."""
    return _native.local_maxima(_array(image), radius)


def watershed(image, markers, *, mask=None, connectivity: int = 8) -> np.ndarray:
    """Marker-controlled minimum-barrier flooding; preserve positive marker IDs.

    Equal-cost paths use queue order; no watershed boundary lines are inserted.
    """
    return _native.watershed(_array(image), _array(markers), _mask(mask), connectivity)


def binary_watershed(
    mask, *, maxima_radius: int = 1, connectivity: int = 8
) -> np.ndarray:
    """Separate binary objects using regional distance maxima and native flooding.

    A zero exterior is included. Every foreground component receives a seed;
    return int32 region labels, with no inserted separator lines.
    """
    return _native.binary_watershed(_mask(mask), maxima_radius, connectivity)


def project(stack, method: str = "maximum", *, first: int = 0,
            last: int | None = None) -> np.ndarray:
    """Project an inclusive, zero-based Z range; ``last=None`` uses the final slice.

    Methods: maximum, minimum, mean, sum, median or population stddev.
    The input is unchanged. Slice indices must be nonnegative integers.
    """
    return _native.project(_array(stack), method, first=first, last=last)


def project_groups(stack, group_size: int, method: str = "mean", *,
                   first: int = 0, last: int | None = None) -> np.ndarray:
    """Project consecutive complete groups in an inclusive zero-based Z range.

    Group size must divide the selected slice count. The returned array always
    retains its Z axis, including one-group outputs. Method/dtype policies are
    the same as :func:`project`; input pixels remain unchanged.
    """
    return _native.project_groups(_array(stack), group_size, method, first=first, last=last)


def gaussian_blur3d(stack, sigma=1, *, border: str = "reflect") -> np.ndarray:
    """Separable volume blur; sigma is a scalar or (Z,Y,X), channels independent."""
    if np.isscalar(sigma):
        sz = sy = sx = sigma
    else:
        values = tuple(sigma)
        if len(values) != 3:
            raise ValueError("3D sigma must be a scalar or (Z,Y,X)")
        sz, sy, sx = values
    return np.stack(_native.gaussian_blur3d(_array(stack), sx, sy, sz, border))


def map_stack(stack, operation: Callable, *args, **kwargs) -> np.ndarray:
    """Apply a native-backed image function independently to each Z slice."""
    array = _array(stack)
    if array.ndim not in (3, 4) or array.shape[0] == 0:
        raise ValueError("Stack must have nonempty shape (Z,H,W) or (Z,H,W,3/4)")
    return np.stack([operation(plane, *args, **kwargs) for plane in array])


def fft2(image, *, pad: bool = True) -> np.ndarray:
    """Native complex128 FFT; zero-pad each dimension to a power of two."""
    return _native.fft2(_array(image), pad)


def ifft2(spectrum) -> np.ndarray:
    """Return the real part of the native inverse FFT as float64."""
    return _native.ifft2(_array(spectrum))


def power_spectrum(
    spectrum, *, log_scale: bool = True, shift: bool = True
) -> np.ndarray:
    """Squared FFT magnitude, optionally log1p-scaled and centered."""
    return _native.power_spectrum(_array(spectrum), log_scale, shift)


def fft_bandpass(image, low_cutoff: float = 0, high_cutoff: float = 0) -> np.ndarray:
    """Gaussian frequency bandpass, in cycles/pixel; zero disables a cutoff."""
    return _native.fft_bandpass(_array(image), low_cutoff, high_cutoff)


from .editor import Editor as Editor
from .io import imread as imread, imwrite as imwrite

from ._native import (NativeWorkflowAnnotationIntent, decode_saved_roi_geometry_request, NativeWorkflowLimits, NativeWorkflowOperation, NativeWorkflowImport,
                      NativeWorkflowExport, NativeWorkflowHistogram, NativeWorkflowHistogramOptions, PreparedNativeWorkflow, prepare_native_workflow)

from ._native import (NativeWorkflowBatchInput, NativeWorkflowBatchOptions, NativeWorkflowBatchState,
                      PreparedNativeWorkflowBatch, prepare_native_workflow_batch,
                      NativeWorkflowBatchHistogram, native_workflow_batch_histogram_csv)

from ._native import NativeWorkflowProgram, NativeWorkflowProgramLimits
from ._native import (RgbDatasetMode, RgbDatasetLayout, ResolvedRgbDatasetCommand,
                      resolve_rgb_dataset_command, validate_rgb_dataset_command_record,
                      rgb_dataset_command_from_record, rebind_rgb_dataset_command,
                      prepare_rgb_dataset)
from ._native import split_color_components, merge_color_components, quantize_indexed_color
from ._native import (IndexedDatasetLayout, ResolvedIndexedDatasetCommand,
    IndexedRgbPaletteSource, ResolvedIndexedRgbCommand,
    resolve_indexed_rgb_command, validate_indexed_rgb_command_record,
    indexed_rgb_command_from_record, rebind_indexed_rgb_command, prepare_indexed_rgb,
    resolve_indexed_dataset_command, validate_indexed_dataset_command_record,
    indexed_dataset_command_from_record, rebind_indexed_dataset_command,
    prepare_indexed_dataset, encode_indexed_palette, classify_indexed_palette, validate_indexed_palette_record,
    indexed_palette)
from ._native import (ColorStackAction, ColorStackLayoutPolicy, ColorStackLayout,
                      ResolvedColorStackCommand, resolve_color_stack_command,
                      validate_color_stack_command_record, color_stack_command_from_record,
                      rebind_color_stack_command, prepare_color_stack)
from ._native import NativeWorkflowBatchSelection, NativeWorkflowBatchReview, review_native_workflow_batch_input
from ._native import NativeWorkflowBatchSetup, NativeWorkflowBatchSetupLimits, save_native_workflow_batch_setup, load_native_workflow_batch_setup


from ._native import manual_threshold_contains as manual_threshold_contains
from ._native import ResolvedRgbContrastApplyCommand, resolve_rgb_contrast_apply_command, validate_rgb_contrast_apply_command_record, rgb_contrast_apply_command_from_record, rebind_rgb_contrast_apply_command, estimate_rgb_contrast_apply_command, execute_rgb_contrast_apply_command, estimate_rgb_contrast_apply_job, submit_rgb_contrast_apply
from ._native import RgbContrastApplyOptions, RgbContrastApplyEstimate, apply_rgb_contrast_imagej, estimate_rgb_contrast_apply, prepare_rgb_contrast_apply

from ._native import RgbHistogramComponent, RgbHistogramOptions, RgbHistogramResult, RgbAutoContrastResult, estimate_rgb_histogram, accumulate_rgb_histogram, rgb_auto_contrast_from_histogram, estimate_rgb_histogram_job, submit_rgb_histogram

from ._native import estimate_rgb_contrast_sequence_job, submit_rgb_contrast_sequence

from ._native import RgbContrastStackStep, RgbContrastStackEstimate, estimate_rgb_contrast_stack, prepare_rgb_contrast_stack

from ._native import ResolvedRgbContrastStackCommand, resolve_rgb_contrast_stack_command, validate_rgb_contrast_stack_command_record, rgb_contrast_stack_command_from_record, rebind_rgb_contrast_stack_command, estimate_rgb_contrast_stack_command, execute_rgb_contrast_stack_command, estimate_rgb_contrast_stack_job, submit_rgb_contrast_stack

from ._native import make_rgb_contrast_stack_workflow_step

from ._native import RgbContrastSessionStep, RgbContrastSessionVisit, RgbContrastSessionPlan, RgbContrastSessionEstimate, estimate_rgb_contrast_session, prepare_rgb_contrast_session

from ._native import ResolvedRgbContrastSessionCommand, resolve_rgb_contrast_session_command, validate_rgb_contrast_session_command_record, rgb_contrast_session_command_from_record, rebind_rgb_contrast_session_command, estimate_rgb_contrast_session_command, execute_rgb_contrast_session_command, estimate_rgb_contrast_session_job, submit_rgb_contrast_session

from ._native import make_rgb_contrast_session_workflow_step, rgb_contrast_session_plan_from_record

from ._native import RgbContrastSessionEditor, RgbContrastSessionTransition

# Shared prepared scientific projection; requests are complete strict mappings.
from ._native import (
    imagej_projection_defaults as imagej_projection_defaults,
    resolve_imagej_projection_record as resolve_imagej_projection_record,
    validate_imagej_projection_record as validate_imagej_projection_record,
    prepare_imagej_projection as prepare_imagej_projection,
    replay_imagej_projection as replay_imagej_projection,
    rebind_imagej_projection_record as rebind_imagej_projection_record,
)

__all__ = [
    name
    for name, value in list(globals().items())
    if not name.startswith("_")
    and (
        getattr(value, "__module__", "").startswith("imagejxx")
        or name.endswith("METHODS")
        or name == "shortcut_registry_version"
    )
]
