"""Display setters retire all-Z thresholds on non-anchor slices as well."""
import imagejxx as ij
import numpy as np
import pytest

@pytest.mark.parametrize("dtype",[np.uint16,np.float32])
@pytest.mark.parametrize("all_z",[False,True])
@pytest.mark.parametrize("operation",["variance","top_hat"])
def test_non_anchor_processor_setter_and_history(operation,all_z,dtype):
    pixels=np.arange(100,dtype=dtype).reshape(2,5,10)
    owner=ij.Document(ij.Dataset(pixels,axes="ZYX"));before=owner.snapshot()
    view=ij.set_manual_threshold_state(before.display,before.dataset,ij.ManualThresholdRange(4,8),ij.PlanePosition(0,0,1),all_z=all_z)
    owner.set_display(view);before=owner.snapshot()
    command=(ij.resolve_rank_command(before,options=ij.RankFilterCommandOptions("variance",1)) if operation=="variance" else ij.resolve_top_hat_command(before,options=ij.TopHatCommandOptions(1)))
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert ("manual_threshold" in prepared.preview.display.properties)==(not all_z)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("ZYX")[1],pixels[1])
    assert owner.snapshot().display.properties==before.display.properties
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    owner.undo();assert owner.snapshot().display.properties==before.display.properties
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"),pixels)
    owner.redo();assert ("manual_threshold" in owner.snapshot().display.properties)==(not all_z)
