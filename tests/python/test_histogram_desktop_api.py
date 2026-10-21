import csv
import io
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

def fixture(tmp_path):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.arange(64,dtype=np.uint16).reshape(2,4,8),axes='ZYX').observe_reads(reads))
    editor=_desktop.Editor(str(tmp_path/'prefs.json'));editor.add_native_document(owner)
    return editor,owner,reads

def action(editor,name,**params):editor.action(name,json.dumps(params))

@pytest.mark.parametrize('field,value',[('bins','0'),('bins','65537'),('bins','-'),('lower','nan'),('upper','inf'),('z','0'),('z','3'),('t','2'),('c','2'),('lower','2x')])
def test_invalid_histogram_drafts_refuse_before_pixel_reads(field,value,tmp_path):
    editor,owner,reads=fixture(tmp_path);n=reads.counts().plane_reads
    action(editor,'show_histogram');assert reads.counts().plane_reads==n
    params=dict(custom=True,bins='4',lower='0',upper='64');params[field]=value
    action(editor,'configure_histogram',**params)
    with pytest.raises((ValueError,IndexError)):action(editor,'calculate_histogram')
    assert reads.counts().plane_reads==n and owner.undo_steps==0

def test_captured_navigation_export_and_stale_source(tmp_path):
    editor,owner,reads=fixture(tmp_path);before=owner.snapshot();action(editor,'show_histogram')
    display=before.display;display.crosshair=[0,0,1];owner.set_display(display)
    action(editor,'configure_histogram',custom=True,bins='4',lower='0',upper='32',roi=False)
    action(editor,'calculate_histogram');path=tmp_path/'histogram.csv';action(editor,'export_histogram_csv',path=str(path))
    rows=list(csv.DictReader(io.StringIO(path.read_text())));assert [int(r['count']) for r in rows]==[8,8,8,8]
    assert owner.undo_steps==0 and owner.snapshot().shares_data(before)
    edit=owner.begin_edit();edit.set_property('external',True);owner.commit(edit);n=reads.counts().plane_reads
    with pytest.raises(RuntimeError):action(editor,'calculate_histogram')
    assert reads.counts().plane_reads==n
    action(editor,'configure_histogram',log=True);second=tmp_path/'second.csv';action(editor,'export_histogram_csv',path=str(second));assert second.read_bytes()==path.read_bytes()
    action(editor,'close_histogram')
    with pytest.raises(ValueError):action(editor,'export_histogram_csv',path=str(tmp_path/'absent.csv'))
    assert not (tmp_path/'absent.csv').exists()

@pytest.mark.parametrize('params',[dict(custom=1),dict(roi='true'),dict(log=0),dict(bins=4),dict(lower=False),dict(extra='x')])
def test_configuration_is_atomic_on_type_or_unknown_field_errors(params,tmp_path):
    editor,owner,reads=fixture(tmp_path);action(editor,'show_histogram')
    with pytest.raises(ValueError):action(editor,'configure_histogram',**params)
    action(editor,'calculate_histogram');path=tmp_path/'raw.csv';action(editor,'export_histogram_csv',path=str(path));assert len(path.read_text().splitlines())==257
    assert owner.undo_steps==0
