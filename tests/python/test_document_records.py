"""Lossless native records, retained Fourier buffers and desktop round trips."""

import json

import imagejxx as ij
import numpy as np
import pytest


@pytest.fixture
def document():
    return ij.Document(ij.Dataset(np.arange(20.0).reshape(4, 5), axes="YX"))


@pytest.fixture
def editor(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def test_nested_records_are_detached_equal_and_merge_by_key(document):
    value = {"series": [{"id": 2**64 - 1, "values": [True, None, 0.5]}]}
    first = document.begin_edit(ij.Change.PIXELS)
    second = document.begin_edit(ij.Change.PIXELS)
    first.set_extension("vendor", value)
    first.set_property("acquisition", value)
    second.set_extension("other", [-(2**63), 2**64 - 1])
    value["series"][0]["id"] = 0
    document.commit(first)
    document.commit(second)
    snapshot = document.snapshot()
    assert snapshot.metadata["extensions"]["vendor"]["series"][0]["id"] == 2**64 - 1
    assert snapshot.metadata["extensions"]["other"] == [-(2**63), 2**64 - 1]
    detached = snapshot.metadata
    detached["properties"]["acquisition"]["series"].clear()
    assert document.snapshot().metadata["properties"]["acquisition"]["series"]
    noop = document.begin_edit()
    noop.set_extension("vendor", snapshot.metadata["extensions"]["vendor"])
    noop.set_property("acquisition", snapshot.metadata["properties"]["acquisition"])
    document.commit(noop)
    assert document.undo_steps == 2
    remove = document.begin_edit()
    remove.remove_extension("other")
    document.commit(remove)
    assert "other" not in document.snapshot().metadata["extensions"]
    document.undo()
    assert document.snapshot().metadata["extensions"] == snapshot.metadata["extensions"]


def test_metadata_cycles_depth_and_invalid_records_are_atomic(document):
    cyclic = {"values": []}
    cyclic["values"].append(cyclic)
    tx = document.begin_edit()
    with pytest.raises(ValueError, match="cycles"):
        tx.set_property("invalid", cyclic)
    nested = 1
    for _ in range(128):
        nested = [nested]
    tx.set_extension("deep", nested)
    with pytest.raises(ValueError, match="128"):
        tx.set_extension("invalid", [nested])
    with pytest.raises(ValueError):
        tx.set_overlays([{"text": "valid"}, {"style": {"width": np.nan}}])
    with pytest.raises(ValueError):
        tx.set_rois(
            [ij.ROI("bad", ij.Selection("point", [(1, 1)]), attributes={"name": "bad"})]
        )
    assert tx.preview().overlays == [] and tx.preview().rois == []
    assert set(tx.preview().metadata["extensions"]) == {"deep"}
    assert document.undo_steps == 0 and not document.snapshot().dirty


def test_annotation_attributes_and_previous_geometry_survive_undo(document):
    attrs = {"color": "red", "style": {"dash": [2, 1], "id": 2**64 - 1}}
    selection = ij.Selection.from_mask(np.eye(4, 5)).with_attributes(attrs)
    roi = ij.ROI("sample", selection, z=0, attributes=attrs)
    overlay = {"kind": "text", "text": "µm", "style": attrs}
    tx = document.begin_edit()
    tx.select(selection)
    tx.set_rois([roi])
    tx.set_overlays([overlay])
    document.commit(tx)
    snapshot = document.snapshot()
    attrs["color"] = "blue"
    assert snapshot.rois[0].attributes["color"] == "red"
    assert snapshot.selection.attributes["color"] == "red"
    detached = snapshot.overlays
    detached[0]["style"]["color"] = "black"
    assert document.snapshot().overlays[0]["style"]["color"] == "red"
    reshape = document.begin_edit()
    reshape.select(None)
    reshape.set_rois([])
    reshape.set_overlays([])
    reshape.replace_dataset(ij.Dataset(np.zeros((2, 2)), axes="YX"))
    document.commit(reshape)
    previous = document.snapshot().previous_selection
    np.testing.assert_array_equal(previous.mask((4, 5)), np.eye(4, 5) * 255)
    with pytest.raises(ValueError, match="shape"):
        document.begin_edit().select(previous)
    document.undo()
    assert document.snapshot().rois[0].attributes == snapshot.rois[0].attributes
    assert document.snapshot().overlays == snapshot.overlays
    np.testing.assert_array_equal(
        document.snapshot().selection.mask((4, 5)), np.eye(4, 5) * 255
    )


@pytest.mark.parametrize("dtype", [np.complex64, np.complex128])
def test_fourier_state_retains_detached_typed_data_and_history(document, dtype):
    values = np.arange(32).reshape(4, 8).astype(dtype) + 1j
    expected = values[:, ::-1].astype(np.complex128)
    state = ij.FourierState(values[:, ::-1], original_shape=(3, 5))
    values[:] = 0
    tx = document.begin_edit()
    tx.set_spectrum(state)
    document.commit(tx)
    snapshot = document.snapshot()
    assert snapshot.spectrum.shape == [4, 8]
    assert snapshot.spectrum.original_shape == [3, 5]
    assert snapshot.spectrum.shares_storage(state)
    np.testing.assert_array_equal(snapshot.spectrum.numpy(), expected)
    detached = snapshot.spectrum.numpy()
    detached[:] = 0
    edit = document.begin_edit()
    edit.set_title("Metadata only")
    document.commit(edit)
    assert document.snapshot().spectrum.shares_storage(state)
    view = document.snapshot().display
    view.properties = {"lut": "Fire", "display_range": [-1, 2]}
    document.set_display(view)
    pixels = document.begin_edit()
    pixels.replace_plane(np.ones((4, 5)))
    document.commit(pixels)
    assert document.snapshot().spectrum is None
    document.undo()
    assert document.snapshot().spectrum.shares_storage(state)
    assert document.snapshot().display.properties == view.properties
    document.redo()
    assert document.snapshot().spectrum is None
    del document
    np.testing.assert_array_equal(snapshot.spectrum.numpy(), expected)


@pytest.mark.parametrize(
    "values,shape",
    [
        (np.zeros((2, 2)), None),
        (np.zeros(4, complex), None),
        (np.zeros((0, 2), complex), None),
        (np.full((2, 2), np.nan + 1j), None),
        (np.zeros((2, 2), complex), (3, 2)),
        (np.zeros((2, 2), complex), (0, 1)),
    ],
)
def test_invalid_fourier_ingestion(values, shape):
    with pytest.raises(ValueError):
        ij.FourierState(values, original_shape=shape)


def test_desktop_retains_full_records_and_legacy_session(editor, tmp_path):
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import serialize_document

    item = serialize_document(Document(np.arange(20.0).reshape(4, 5), "Original"))
    style = {"color": "red", "extension": {"id": 2**64 - 1, "flags": [None, True]}}
    selection = {"kind": "point", "points": [[1, 1]], "width": 2, "style": style}
    item.update(
        {
            "selection": selection,
            "previous_selection": selection,
            "rois": [
                {"name": "sample", "selection": selection, "slice_index": 0, **style}
            ],
            "overlays": [
                {"kind": "text", "text": "label", "points": [[1, 1]], **style}
            ],
            "display_range": [-1, 20],
            "lut": "Fire",
            "native_properties": {"nested": {"values": [2**64 - 1, None, 0.5]}},
            "vendor": {"axes": ["Y", "X"], "id": 2**64 - 1},
        }
    )
    item["calibration"]["intensity"] = {"coefficients": [0, 1.25], "unit": "a.u."}
    editor._editor.add_document(json.dumps(item))
    live = editor.live_document()
    snapshot = live.native_snapshot()
    assert snapshot.selection.attributes == {"style": style}
    assert snapshot.previous_selection.attributes == {"style": style}
    assert snapshot.rois[0].attributes == style
    assert snapshot.overlays == item["overlays"]
    assert snapshot.display.properties == {
        "display_range": [-1, 20],
        "lut": "Fire",
        "custom_lut": None,
    }
    assert snapshot.metadata["properties"] == item["native_properties"]
    assert snapshot.metadata["extensions"]["vendor"] == item["vendor"]
    assert snapshot.metadata["extensions"]["calibration"] == {
        "intensity": item["calibration"]["intensity"]
    }
    edit = live.begin_edit()
    edit.set_title("Changed")
    edit.set_property("extra", [True, {"id": 2**64 - 1}])
    edit.set_overlays(
        snapshot.overlays + [{"kind": "text", "text": "second", "points": [[2, 1]]}]
    )
    live.commit(edit)
    raw = json.loads(editor._editor.document_snapshot(live.id))
    for key in ["vendor", "selection", "previous_selection", "rois"]:
        assert raw[key] == item[key]
    assert raw["calibration"]["intensity"] == item["calibration"]["intensity"]
    assert raw["native_properties"]["nested"] == item["native_properties"]["nested"]
    assert len(raw["overlays"]) == 2
    editor.action("undo")
    assert live.native_snapshot().overlays == snapshot.overlays
    editor.action("redo")
    path = tmp_path / "full-records.ijx"
    editor.action("save_workspace", path=str(path))
    editor.open_file(path)
    restored = editor.live_document().native_snapshot()
    assert restored.metadata == live.native_snapshot().metadata
    assert restored.overlays == live.native_snapshot().overlays
    assert restored.rois[0].attributes == style
    assert restored.display.properties == snapshot.display.properties


def test_desktop_record_rejection_is_atomic(editor):
    from imagejxx.ui.model import Document

    editor.add_document(Document(np.zeros((4, 5))))
    live = editor.live_document()
    before = live.info
    for key, value in [
        ("selection", {"kind": "point"}),
        ("calibration", {"pixel_width": 2}),
        ("calibration", 1),
    ]:
        tx = live.begin_edit()
        tx.set_extension(key, value)
        tx.set_title("Must not publish")
        with pytest.raises(ValueError):
            live.commit(tx)
        assert tx.active and live.info == before
    view = live.native_snapshot().display
    view.properties = {"custom_lut": [[0, 0, 0]] * 255 + [[256, 0, 0]]}
    with pytest.raises(ValueError, match="0..255"):
        live.set_display(view)
    assert live.info == before


def test_desktop_native_display_targets_one_document_during_async_edit(
    editor, tmp_path
):
    from imagejxx.ui.model import Document

    pixels = np.arange(60.0).reshape(3, 4, 5)
    editor.add_document(Document(pixels, is_stack=True))
    live = editor.live_document()
    editor.add_document(Document(np.zeros((4, 5)), "Other"))
    other = editor.live_document()
    editor.action("save_workspace", path=str(tmp_path / "clean.ijx"))
    source = live.native_snapshot()
    live.apply_operation("add", {"value": 1}, asynchronous=True)
    view = source.display
    view.crosshair = [2, 1, 2]
    view.properties = {"display_range": [2, 50], "lut": "Ice"}
    live.set_display(view)
    assert live.info["undo_steps"] == 1 and not live.info["dirty"]
    assert live.native_snapshot().revisions.matches(source.revisions)
    assert other.native_snapshot().display.properties == {
        "lut": "Grays",
        "display_range": None,
        "custom_lut": None,
    }
    editor.wait()
    expected = pixels.copy()
    expected[0] += 1
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), expected)
    editor.active_index = 0
    editor.action("undo")
    assert live.native_snapshot().display.properties == view.properties
    assert live.native_snapshot().display.crosshair == [2, 1, 2]


def test_desktop_fourier_state_round_trip_retention_and_inverse(editor, tmp_path):
    from imagejxx.ui.model import Document

    pixels = np.arange(15.0).reshape(3, 5)
    editor.add_document(Document(pixels))
    editor.action("fft")
    editor.wait()
    live = editor.live_document()
    retained = live.native_snapshot()
    expected = ij.fft2(pixels)
    assert retained.spectrum.original_shape == [3, 5]
    np.testing.assert_array_equal(retained.spectrum.numpy(), expected)
    edit = live.begin_edit()
    edit.set_extension("vendor", {"fft": True})
    live.commit(edit)
    assert live.native_snapshot().spectrum.shares_storage(retained.spectrum)
    path = tmp_path / "fft.ijx"
    editor.action("save_workspace", path=str(path))
    editor.open_file(path)
    reopened = editor.live_document()
    np.testing.assert_array_equal(reopened.native_snapshot().spectrum.numpy(), expected)
    assert reopened.native_snapshot().spectrum.original_shape == [3, 5]
    editor.action("inverse_fft")
    np.testing.assert_allclose(editor.live_document().plane(), pixels, atol=1e-10)
    pixels_edit = live.begin_edit()
    pixels_edit.replace_plane(np.zeros(retained.dataset.shape[-3:-1]))
    live.commit(pixels_edit)
    assert live.native_snapshot().spectrum is None
    live.close(discard=True)
    np.testing.assert_array_equal(retained.spectrum.numpy(), expected)
