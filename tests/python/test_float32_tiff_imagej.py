"""Interoperability with actual pinned ImageJ FileSaver/Opener carriers."""
from pathlib import Path
import json
import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
ROOT=Path(__file__).parents[1]/'fixtures/float32-tiff-imagej154r'
ROWS=json.loads((ROOT/'SOURCE.json').read_bytes())['files']
@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_actual_imagej_file_words_dimensions_and_processing(tmp_path,row):
    assert row['source_hex']==row['reopened_hex']
    read=io.ImageReadRequest();read.sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    info=io.inspect_image_file(ROOT/row['file'])
    result=io.read_image_series(ROOT/row['file'],request=read)
    assert result.dataset.shape==tuple(row['shape'])
    assert result.dataset.numpy('TCZYX').tobytes().hex()==row['reopened_hex']
    assert tuple(result.dataset.calibration.spacing)==tuple(row['spacing'])
    owner=ij.Document(result.dataset);command=ij.resolve_remove_nans_command(owner.snapshot(),options=ij.RemoveNaNsCommandOptions(1))
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters());owner.commit(prepared.begin_edit())
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes().hex()==row['filtered_hex']
