"""Session callbacks cannot rebind relative paths or damage a NUL prefix file."""
from pathlib import Path
import os
import imagejxx as ij
import numpy as np
import pytest


def capture(title='captured'):
    owner=ij.Document(ij.Dataset(np.arange(6,dtype=np.uint16).reshape(2,3),axes='YX'),title)
    return owner,ij.SessionCapture([owner.snapshot()])


@pytest.mark.parametrize('phase',['encode','replace'])
def test_session_output_is_frozen_before_first_and_final_callbacks(tmp_path,monkeypatch,phase):
    a=tmp_path/'original';b=tmp_path/'other';a.mkdir();b.mkdir();monkeypatch.chdir(a)
    target=a/'output.ijx';other=b/'output.ijx';other.write_bytes(b'unrelated target')
    owner,data=capture();before=owner.snapshot();called=[]
    def checkpoint():
        if not called:os.chdir(b);called.append('encode')
    def progress(stage,done,total):
        if stage=='Atomic file replace':os.chdir(b);called.append('replace')
    ij.save_session('output.ijx',data,**({'checkpoint':checkpoint} if phase=='encode' else {'stage_progress':progress}))
    assert called and ij.load_session(str(target)).documents[0].snapshot().metadata['title']=='captured'
    assert other.read_bytes()==b'unrelated target'
    assert owner.snapshot().shares_data(before) and not owner.snapshot().dirty and owner.undo_steps==0
    assert not any('.tmp.' in p.name for directory in (a,b) for p in directory.iterdir())


def test_session_input_is_frozen_before_first_checkpoint(tmp_path,monkeypatch):
    a=tmp_path/'original';b=tmp_path/'other';a.mkdir();b.mkdir()
    _,data=capture('expected');_,unrelated=capture('wrong directory')
    ij.save_session(str(a/'input.ijx'),data);ij.save_session(str(b/'input.ijx'),unrelated)
    monkeypatch.chdir(a);called=[]
    def checkpoint():
        if not called:os.chdir(b);called.append(True)
    loaded=ij.load_session('input.ijx',checkpoint=checkpoint)
    assert called and loaded.documents[0].snapshot().metadata['title']=='expected'


@pytest.mark.parametrize('operation',['save','load'])
@pytest.mark.parametrize('kind',['empty','nul'])
def test_invalid_session_path_refuses_before_callbacks(tmp_path,operation,kind):
    path=tmp_path/'prefix.ijx';path.write_bytes(b'unrelated prefix')
    bad='' if kind=='empty' else str(path)+'\x00tail'
    _,data=capture();called=[]
    with pytest.raises(ValueError):
        if operation=='save':ij.save_session(bad,data,checkpoint=lambda:called.append(True))
        else:ij.load_session(bad,checkpoint=lambda:called.append(True))
    assert not called and path.read_bytes()==b'unrelated prefix'


def test_precancel_still_refuses_without_hooks_or_output(tmp_path):
    path=tmp_path/'target.ijx';path.write_bytes(b'existing target')
    _,data=capture();token=ij.CancellationToken();token.cancel();called=[]
    with pytest.raises(ij.OperationCancelled):ij.save_session(str(path),data,cancellation=token,checkpoint=lambda:called.append(True))
    with pytest.raises(ij.OperationCancelled):ij.load_session(str(path),cancellation=token,checkpoint=lambda:called.append(True))
    assert not called and path.read_bytes()==b'existing target' and set(tmp_path.iterdir())=={path}
