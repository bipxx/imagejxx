from copy import deepcopy

import pytest

from imagejxx.ui.model import NamedROI, Selection


@pytest.mark.parametrize("slice_index", [None, 3])
def test_named_roi_preserves_legacy_record_and_custom_attributes(slice_index):
    legacy = NamedROI("region", Selection("rectangle", [(2, 2), (5, 4)]), slice_index)
    record = legacy.to_dict()
    assert set(record) == {"name", "selection", "slice_index"}
    assert NamedROI.from_dict(record).to_dict() == record

    record.update(group=2, labels={"tags": ["first", "second"]})
    expected = deepcopy(record)
    restored = NamedROI.from_dict(record)
    record["labels"]["tags"].append("changed input")
    assert restored.to_dict() == expected
    exported = restored.to_dict()
    exported["labels"]["tags"].append("changed export")
    assert restored.to_dict() == expected


@pytest.mark.parametrize("reserved", ["name", "selection", "slice_index"])
def test_named_roi_custom_attributes_cannot_replace_identity_or_geometry(reserved):
    roi = NamedROI("region", Selection("rectangle", [(2, 2), (5, 4)]), 3)
    roi.attributes[reserved] = "replacement"
    with pytest.raises(ValueError, match="reserved"):
        roi.to_dict()
