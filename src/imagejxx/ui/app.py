"""Optional Python interface to the shared C++ Dear ImGui desktop.

The native editor owns live UI documents. ``documents`` returns detached Python
snapshots, so scripts cannot race the processing worker or the rendering loop.
Importing the base ``imagejxx`` package never loads this optional module.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from .commands import json_values
from .model import Document, Selection
from .persistence import Preferences, _deserialize_document, serialize_document


def _native_ui():
    try:
        from imagejxx import _desktop
    except ImportError as error:
        raise ImportError(
            "The optional Dear ImGui desktop is not installed. Build imagejxx with "
            "-Ccmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON and the reviewed ImGui dependencies. "
            "See docs/DESKTOP.md for the build commands."
        ) from error
    return _desktop


def _json(value):
    return json.dumps(json_values(value), allow_nan=False)


def _document_payload(document):
    if not isinstance(document, Document):
        document = Document(document)
    state = document._session_native_state()
    payload = {"document": serialize_document(document)}
    if state is not None:
        payload["native_state"] = {
            "display_properties": [key for key in ("display_range", "lut", "custom_lut")
                                   if key in state["display_properties"]],
            "metadata": state["metadata"],
        }
    return payload


class Editor:
    """Own the native ImGui editor; launch on the main thread with ``run()``.

    All pixel operations and both UI launch modes use the same C++ controller.
    The Python document models remain available for scripting and file workflows.
    """

    def __init__(
        self, *, preferences=None, images=None, workspace=None,
        restore_workspace=None, reset_layout=False,
    ):
        """Resolve one native startup request before the first frame.

        ``preferences`` selects a path; a Preferences object's in-memory values
        are not applied. ``images`` contains file paths. Native code validates
        the complete request and stages its documents as one batch.
        TIFF paths open asynchronous review before any document is attached;
        each needs scientific axes confirmation or explicit flat-pixel consent.
        """
        preferences_path = preferences.path if isinstance(preferences, Preferences) else preferences
        if preferences_path is None:
            preferences_path = ""
        elif isinstance(preferences_path, Path):
            preferences_path = str(preferences_path)
        if type(images) in (list, tuple):
            images = [str(path) if isinstance(path, Path) else path for path in images]
        if isinstance(workspace, Path):
            workspace = str(workspace)
        self._editor = _native_ui().Editor(
            preferences_path=preferences_path, images=images, workspace=workspace,
            restore_workspace=restore_workspace, reset_layout=reset_layout,
        )

    @property
    def startup_state(self):
        """Return the native startup plan, diagnostics and actual membership."""
        return self._editor.startup_state

    def retry_startup(self):
        """Read the selected workspace again while the native window is empty and idle."""
        return self._editor.retry_startup()

    def start_empty_startup(self):
        """Dismiss a failed restore for this launch, preserving its configuration."""
        return self._editor.start_empty_startup()

    def disable_startup_restore(self):
        """Persist the native Startup-group reset without changing other preferences."""
        return self._editor.disable_startup_restore()

    def reset_layout(self, *, persist=True):
        """Reset the actual native layout, optionally saving removal of its stored value."""
        return self._editor.reset_layout(persist)

    @property
    def settings(self):
        """Return a detached, validated settings value; use update_settings to apply."""
        from imagejxx import Settings

        return Settings(json.loads(self._editor.settings()))

    @property
    def render_memory_info(self):
        """Detached managed render reservations; logical textures exclude driver overhead."""
        return json.loads(self._editor.render_memory_info())

    @property
    def virtual_view_cache_info(self):
        """Detached decoded XY cache counters; external handles/render memory excluded."""
        return json.loads(self._editor.virtual_view_cache_info())

    @property
    def shortcut_profile(self):
        """Return the immutable native profile applied to this editor."""
        return self._editor.shortcut_profile()

    @property
    def preferences(self):
        """Return the versioned preferences document, including preserved extras."""
        return json.loads(self._editor.preferences())

    def update_settings(self, values=None, *, persist=True, **changes):
        """Atomically validate and apply settings to the shared native editor."""
        patch = dict(values or {})
        patch.update(changes)
        self._editor.update_settings(_json(patch), persist)

    def _detached_document(self, item, document_id):
        snapshot = self._editor.native_snapshot(document_id)
        metadata = snapshot.metadata
        metadata["record_layout"] = snapshot.record_layout
        state = {
            "metadata": metadata,
            "display_properties": [key for key in ("display_range", "lut", "custom_lut")
                                   if key in snapshot.display.properties],
        }
        return _deserialize_document(item, native_state=state)

    @property
    def documents(self):
        items = json.loads(self._editor.documents())
        return [
            self._detached_document(item, info["id"])
            for item, info in zip(items, self.document_infos, strict=True)
        ]

    @property
    def document(self):
        return self.document_snapshot()

    def document_snapshot(self, document_id=0):
        """Return one detached snapshot; other documents are never serialized."""
        return self._detached_document(
            json.loads(self._editor.document_snapshot(document_id)), document_id,
        )

    @property
    def document_infos(self):
        """Inspect document identity, axes, view and history without pixel copies."""
        return json.loads(self._editor.document_infos())

    def live_document(self, document_id=0):
        """Return a stable handle with targeted native reads and editing actions."""
        info = json.loads(self._editor.document_info(document_id))
        return LiveDocument(self, info["id"])

    @property
    def active_index(self):
        return self._editor.active_index

    @active_index.setter
    def active_index(self, value):
        self._editor.active_index = int(value)

    @property
    def recording(self):
        return self._editor.recording

    @recording.setter
    def recording(self, value):
        self._editor.recording = bool(value)

    @property
    def results(self):
        return json.loads(self._editor.results)

    @results.setter
    def results(self, value):
        self._editor.results = _json(value)

    @property
    def workflow(self):
        return json.loads(self._editor.workflow)

    @workflow.setter
    def workflow(self, value):
        self._editor.workflow = _json(value)

    @property
    def busy(self):
        return self._editor.busy

    @property
    def job_info(self):
        return self._editor.job_info

    @property
    def jobs(self):
        return self._editor.jobs

    def cancel_job(self, job_id):
        return self._editor.cancel_job(job_id)

    @property
    def error(self):
        return self._editor.error

    def add_document(self, document):
        payload = _document_payload(document)
        self._editor.add_document(
            _json(payload["document"]),
            _json(payload["native_state"]) if "native_state" in payload else "",
        )

    def append_launch_items(self, images):
        """Start one fresh mixed launch; TIFF review pauses ordered native append.

        At most 64 items are captured before any item opens. Relative paths are
        resolved on admission, and array/Document data is detached from the caller.
        Cancel or a file failure retains the accepted prefix and discards the rest.
        """
        if isinstance(images, (str, Path)):
            raise TypeError("Launch items must be a sequence of paths and images")
        images = list(images)
        if not 1 <= len(images) <= 64:
            raise ValueError("Mixed launch requires between one and 64 items")
        items = [{"path": str(image)} if isinstance(image, (str, Path))
                 else _document_payload(image) for image in images]
        self.action("queue_launch", items=items)

    @property
    def launch_state(self):
        """Return mixed-launch progress without polling or granting consent."""
        return self.startup_state.get("append")

    def add_native_document(self, document):
        """Attach an existing C++ document owner without serializing its pixels."""
        self._editor.add_native_document(document)
        return self.live_document(document.id)

    def open_file(self, path):
        """Open TIFF into review; wait for inspection, then confirm axes or flat pixels."""
        self._editor.open_file(str(path))

    def open_legacy_file(self, path):
        """Explicit synchronous pixel import; TIFF pages use flat Z and pixel units."""
        self._editor.open_legacy_file(str(path))

    def close_document(self, document_id=0, *, discard=False):
        """Close one document; unsaved changes require an explicit discard."""
        self._editor.close_document(document_id, discard)

    def select(self, selection=None):
        self._editor.select(
            _json(
                selection.to_dict() if isinstance(selection, Selection) else selection
            )
        )

    def apply_operation(
        self,
        operation,
        parameters=None,
        all_slices=False,
        *,
        asynchronous=True,
        document_id=0,
    ):
        self._editor.apply_operation(
            operation, _json(parameters or {}), all_slices, asynchronous, document_id
        )

    def apply_gaussian(self, target_request=None, options=None, *, document_id=0, wait=False):
        """Apply captured native C/Z/T targets to the active document.

        Requests use zero-based native coordinates. The shared worker performs
        image processing; one accepted edit creates one native history entry.
        """
        from .. import TargetRequest, GaussianBlurCommandOptions

        self._editor.apply_gaussian(
            TargetRequest() if target_request is None else target_request,
            GaussianBlurCommandOptions() if options is None else options,
            document_id,
        )
        if wait:
            self.wait()

    def apply_rank(self, target_request=None, options=None, *, document_id=0, wait=False):
        """Apply an ImageJ rank method through the shared native editor lanes.

        Target coordinates are zero-based. Options select minimum, maximum,
        median or mean and a fractional radius in pixels.
        """
        from .. import TargetRequest, RankFilterCommandOptions

        self._editor.apply_rank(
            TargetRequest() if target_request is None else target_request,
            RankFilterCommandOptions() if options is None else options,
            document_id,
        )
        if wait:
            self.wait()

    def apply_remove_nans(self, target_request=None, options=None, *, document_id=0, wait=False):
        """Replace Float32 NaNs through the shared native job lanes, including opted IEEE sources."""
        from .. import TargetRequest, RemoveNaNsCommandOptions, resolve_remove_nans_command
        command = resolve_remove_nans_command(self._editor.native_snapshot(document_id),
            TargetRequest() if target_request is None else target_request,
            RemoveNaNsCommandOptions() if options is None else options)
        self.action("rank_replay", record=command.record(), document_id=document_id)
        if wait:
            self.wait()

    def apply_top_hat(self, target_request=None, options=None, *, document_id=0, wait=False):
        """Capture active LUT polarity and apply Top Hat through native jobs."""
        from .. import TargetRequest, TopHatCommandOptions, resolve_top_hat_command
        command = resolve_top_hat_command(self._editor.native_snapshot(document_id),
            TargetRequest() if target_request is None else target_request,
            TopHatCommandOptions() if options is None else options)
        self.action("rank_replay", record=command.record(), document_id=document_id)
        if wait:
            self.wait()

    def apply_remove_outliers(self, target_request=None, options=None, *, document_id=0, wait=False):
        """Capture active LUT polarity and apply through the shared native jobs."""
        from .. import TargetRequest, RemoveOutliersCommandOptions, resolve_remove_outliers_command
        command = resolve_remove_outliers_command(
            self._editor.native_snapshot(document_id),
            TargetRequest() if target_request is None else target_request,
            RemoveOutliersCommandOptions() if options is None else options)
        self.action("rank_replay", record=command.record(), document_id=document_id)
        if wait:
            self.wait()

    def apply_despeckle(self, target_request=None, *, document_id=0, wait=False):
        """Apply the fixed ImageJ median-radius-one preset through C++ jobs."""
        from .. import RankFilterCommandOptions
        self.apply_rank(target_request, RankFilterCommandOptions("median", 1),
                        document_id=document_id, wait=wait)

    def enable_channel_display(self, document_id=0):
        """Enable native channel display for an explicitly owned document."""
        return self._editor.enable_channel_display(document_id=document_id)

    def apply_channel_display(self, request, *, document_id=0):
        """Apply a full native ``action``, ``state``, ``channels`` request.

        Channel indices are zero-based. The C++ core validates the complete
        state and resolves reset ranges from the captured live XY planes.
        """
        return self._editor.apply_channel_display(request, document_id=document_id)

    def replay_channel_display(self, record, *, rebind_request=None, document_id=0):
        """Replay recorded ranges, or explicitly bind a full request to an owner."""
        return self._editor.replay_channel_display(
            record, rebind_request=rebind_request, document_id=document_id
        )

    def apply_selection_creation(self, request, *, document_id=0):
        """Publish one complete native area candidate against an owned document."""
        return self._editor.apply_selection_creation(request, document_id=document_id)

    def replay_selection_creation(self, record, *, rebind_request=None, document_id=0):
        """Replay resolved geometry, or explicitly supply a complete new request."""
        return self._editor.replay_selection_creation(
            record, rebind_request=rebind_request, document_id=document_id
        )

    def apply_saved_roi_geometry(self, request):
        """Edit one saved area through the shared native controller."""
        return self._editor.apply_saved_roi_geometry(request)

    def replay_saved_roi_geometry(self, record, *, complete_rebind_request=None):
        """Replay a bound edit or explicitly supply the full replacement request."""
        return self._editor.replay_saved_roi_geometry(
            record, complete_rebind_request=complete_rebind_request
        )

    def replay_axis_extraction(self, record, *, rebind_request=None, document_id=0):
        """Replay an exact native extraction or explicitly bind a full new request.

        Strict replay retains recorded channel/time coordinates. Rebinding uses
        the destination's captured position and the complete typed request.
        """
        return self._editor.replay_axis_extraction(
            record, rebind_request=rebind_request, document_id=document_id
        )

    def replay_axis_orientation(self, record, *, rebind_request=None, document_id=0):
        """Replay native orientation, or bind a complete new typed request."""
        return self._editor.replay_axis_orientation(
            record, rebind_request=rebind_request, document_id=document_id
        )

    def action(self, command=None, /, **parameters):
        """Dispatch a command with separate ``action`` and ``name`` parameters.

        Legacy ``action(action="command", ...)`` and ``action(name="command", ...)``
        spellings remain supported when no positional command is supplied.
        """
        if command is None:
            command = parameters.pop("action", None)
        if command is None:
            command = parameters.pop("name", None)
        if command is None:
            raise TypeError("action() requires an action name")
        self._editor.action(command, _json(parameters))

    def poll(self):
        self._editor.poll()

    def wait(self):
        self._editor.wait()
        if self.error:
            error = self.error
            self._editor.clear_error()
            raise RuntimeError(error)

    def cancel(self):
        self._editor.cancel()

    def run(self, *, hidden=False, frames=0, screenshot=""):
        """Run the desktop loop; a bounded hidden run is useful for render checks."""
        return self._editor.run(hidden, frames, str(screenshot))

    def render_headless(
        self, frames=3, *, exercise=False, camera=False, events=None,
        exercise_preferences=False, exercise_orthogonal_busy=False,
    ):
        """Render ImGui frames, with an optional sequence of actual input events.

        Events use ``type``: position (point, optional image/screen space),
        button/key (name, down), UTF-8 text (value), action (name, parameters), or wait. Each has
        an optional frame count. Returned input_trace captures source/preview
        selections after each event. Built-in exercises and events are exclusive.
        ``exercise_preferences`` opens Edit/Preferences, searches by keyboard,
        toggles a visible checkbox and presses Apply using actual ImGui input.
        ``exercise_orthogonal_busy`` holds a real diagnostic job while supplied
        events run, recording keyboard admission during the running operation.
        """
        return json.loads(
            self._editor.render_headless(
                frames, exercise, camera, _json([] if events is None else events),
                exercise_preferences, exercise_orthogonal_busy,
            )
        )

    def smoke_test(self, directory):
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        return self._editor.smoke_test(str(directory))


class LiveDocument:
    def show_threshold(self):
        """Open the shared native manual threshold panel for this document."""
        self._editor.action("show_manual_threshold", document_id=self.id)

    def set_threshold(self, lower, upper):
        """Set inclusive raw bounds without changing pixels or pixel history."""
        self._editor.action("set_manual_threshold", document_id=self.id,
                            lower=lower, upper=upper)

    def reset_threshold(self):
        self._editor.action("reset_manual_threshold", document_id=self.id)

    def threshold_to_selection(self, lower, upper):
        self._editor.action("manual_threshold_to_selection", document_id=self.id,
                            lower=lower, upper=upper)

    def apply_threshold(self, lower, upper):
        """Apply the captured current-plane/ROI mask through the C++ executor."""
        self._editor.action("apply_manual_threshold", document_id=self.id,
                            lower=lower, upper=upper)

    """Creator-thread access to one native document, independent of active tabs.

    Plane reads return detached arrays. Mutations use explicit native actions;
    Accessing a closed document fails instead of selecting a new tab.
    """

    def __init__(self, editor, document_id):
        self._editor = editor
        self.id = int(document_id)

    @property
    def info(self):
        return json.loads(self._editor._editor.document_info(self.id))

    @property
    def view(self):
        return self.info["view"]

    def set_view(self, **parameters):
        self._editor.action("view", **parameters, document_id=self.id)

    def fit_selection(self, *, extent=None):
        """Fit the XY selection using its canvas, or an explicit logical-pixel extent."""
        parameters = {} if extent is None else {"extent": extent}
        self._editor.action("fit_selection", **parameters, document_id=self.id)

    def native_snapshot(self):
        """Immutable native dataset/metadata/ROI snapshot without pixel serialization."""
        return self._editor._editor.native_snapshot(self.id)

    @property
    def native_owner(self):
        """Shared headless owner; edits while attached follow UI thread rules."""
        return self._editor._editor.native_document(self.id)

    def set_display(self, display):
        """Apply a detached native display record without pixel history or dirtying."""
        self._editor._editor.set_native_display(display, self.id)

    def begin_edit(self, dependencies=None):
        """Stage edits using the headless C++ transaction contract."""
        if dependencies is None:
            return self._editor._editor.begin_edit(self.id)
        return self._editor._editor.begin_edit(self.id, dependencies)

    def resolve_target(self, request=None, *, operation=""):
        """Capture positions and ROI policy without reading any pixel planes."""
        from imagejxx import TargetRequest, resolve_target

        return resolve_target(
            self.native_snapshot(),
            TargetRequest() if request is None else request,
            operation,
        )

    def commit(self, transaction):
        """Commit this document's transaction; stale or foreign edits are rejected."""
        if transaction.document_id != self.id:
            raise ValueError("Transaction belongs to another document")
        self._editor._editor.commit_edit(transaction)

    def close(self, *, discard=False):
        """Close this document while retaining any existing native snapshots."""
        self._editor.close_document(self.id, discard=discard)

    def plane(self, view="xy", coordinate=None):
        return self._editor._editor.plane(
            view, -1 if coordinate is None else coordinate, self.id
        )

    def snapshot(self):
        return self._editor.document_snapshot(self.id)

    def export_plane(self, view="xy", coordinate=None):
        """Open a calibrated plane through the C++ core; coordinates are zero-based."""
        self._editor.action(
            "export_axis_plane", plane=view, coordinate=coordinate,
            document_id=self.id,
        )
        return self._editor.live_document()

    def reslice_range(self, plane, first, last, step=1):
        """Create a range stack through C++; endpoints are inclusive and zero-based.

        The shared C++ dialog displays one-based First/Last and converts them
        once. This direct action already receives native zero-based endpoints.
        Positive-step validation and all pixel/calibration work belong to C++.
        """
        self._editor.action(
            "reslice_axis_range", plane=plane, first=first, last=last, step=step,
            document_id=self.id,
        )
        return self._editor.live_document()

    def reslice_oriented_range(self, plane, first, last, step=1, *,
                               reverse_order=False, flip_vertical=False, transpose=False):
        """Create a C++ oriented range; endpoints are inclusive and zero-based.

        Flip applies to the input vertical axis before transpose. Transpose
        exchanges axes, matching ImageJ's Rotate90 output option. It is an
        axis exchange; selected scientific T/C remain explicit in the core API.
        """
        self._editor.action(
            "reslice_oriented_axis_range", plane=plane, first=first, last=last,
            step=step, reverse_order=reverse_order, flip_vertical=flip_vertical,
            transpose=transpose, document_id=self.id,
        )
        return self._editor.live_document()

    def reslice_slicer_range(self, plane, first, last, step=1, *,
                            reverse_order=False, flip_vertical=False, transpose=False):
        """Reslice ImageJ's bilinear pixel grid through the C++ core.

        Indices are zero-based. Dataset coordinates follow the nominal sampling
        affine; the separate ImageJ scalar spacing does not define that affine.
        """
        self._editor.action(
            "reslice_slicer_axis_range", plane=plane, first=first, last=last,
            step=step, reverse_order=reverse_order, flip_vertical=flip_vertical,
            transpose=transpose, document_id=self.id,
        )
        return self._editor.live_document()

    def reslice_spaced_range(self, plane, first, last, step=1, *,
                             physical_vertical_spacing, sampling="nearest",
                             reverse_order=False, flip_vertical=False, transpose=False):
        """Reslice a physical grid through C++; range indices are zero-based.

        Physical spacing applies to source Z before transpose. Validation,
        sampling, calibration and publication remain in the C++ core.
        """
        self._editor.action(
            "reslice_spaced_axis_range", plane=plane, first=first, last=last,
            step=step, physical_vertical_spacing=physical_vertical_spacing,
            sampling=sampling, reverse_order=reverse_order,
            flip_vertical=flip_vertical, transpose=transpose, document_id=self.id,
        )
        return self._editor.live_document()

    def select(self, selection=None):
        value = selection.to_dict() if isinstance(selection, Selection) else selection
        self._editor._editor.select_document(self.id, _json(value))

    def combine_rois(self, indices, operation="union"):
        """Combine saved area ROIs through the shared C++ manager action."""
        self._editor.action(
            "combine_rois", indices=list(indices), operation=operation,
            document_id=self.id,
        )

    def apply_operation(
        self, operation, parameters=None, all_slices=False, *, asynchronous=True
    ):
        self._editor.apply_operation(
            operation,
            parameters,
            all_slices,
            asynchronous=asynchronous,
            document_id=self.id,
        )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="imagejxx Dear ImGui desktop over the C++ core"
    )
    parser.add_argument("images", nargs="*")
    parser.add_argument("--preferences", type=Path)
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--no-restore-workspace", action="store_true")
    parser.add_argument("--reset-layout", action="store_true")
    parser.add_argument("--smoke-test", action="store_true")
    parser.add_argument("--smoke-dir", type=Path, default=Path("build/python-ui-smoke"))
    parser.add_argument("--hidden", action="store_true")
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--screenshot", default="")
    parser.add_argument("--startup-observe", type=Path)
    args = parser.parse_args(argv)
    if args.workspace is not None and (args.images or args.no_restore_workspace):
        parser.error("--workspace cannot be combined with images or --no-restore-workspace")
    if args.smoke_test and (
        args.images or args.preferences is not None or args.workspace is not None
        or args.no_restore_workspace or args.reset_layout or args.startup_observe is not None
    ):
        parser.error("--smoke-test cannot be combined with normal startup arguments")
    if args.startup_observe is not None:
        if not args.hidden or not 1 <= args.frames <= 180:
            parser.error("--startup-observe requires --hidden and --frames 1..180")
        if args.startup_observe.exists():
            parser.error("Startup observation output must be fresh")
    try:
        editor = Editor(
            preferences=args.smoke_dir / "preferences.json" if args.smoke_test else args.preferences,
            images=args.images or None,
            workspace=args.workspace,
            restore_workspace=False if args.smoke_test or args.no_restore_workspace else None,
            reset_layout=args.reset_layout,
        )
        if args.smoke_test:
            return editor.smoke_test(args.smoke_dir)
        result = editor.run(
            hidden=args.hidden, frames=args.frames, screenshot=args.screenshot
        )
        if args.startup_observe is not None:
            import hashlib
            import os
            from imagejxx import _native, _desktop
            from imagejxx.ui import persistence
            import imagejxx

            observed = json.loads(editor._editor._startup_observation())
            modules = {"imagejxx": imagejxx, "imagejxx._native": _native,
                       "imagejxx._desktop": _desktop, "imagejxx.ui.app": sys.modules[__name__],
                       "imagejxx.ui.persistence": persistence}
            origins = {}
            for name, module in modules.items():
                source = Path(module.__file__).absolute()
                data = source.read_bytes()
                origins[name] = {"path": source.as_posix(), "bytes": len(data),
                                 "sha256": hashlib.sha256(data).hexdigest()}
            observed["module_origins"] = origins
            observed["python_process_id"] = os.getpid()
            with args.startup_observe.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(json.dumps(observed, allow_nan=False))
                stream.write("\n")
        return result
    except (ImportError, RuntimeError, ValueError, TypeError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
