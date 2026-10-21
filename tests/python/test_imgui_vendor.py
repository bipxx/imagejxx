"""Pinned ImGui source and MIT review reject unreviewed payload changes."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import pytest

@pytest.mark.parametrize('change',['none','noncommercial','manifest_hash','archive','changed_header','missing_license','extra_source','traversal'])
def test_imgui_complete_source_review(tmp_path,monkeypatch,change):
    root=Path(__file__).resolve().parents[2]
    spec=importlib.util.spec_from_file_location('imgui_license_policy',root/'scripts/check_licenses.py')
    gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
    vendor=tmp_path/'third_party/imgui';shutil.copytree(root/'third_party/imgui',vendor)
    (tmp_path/'licenses').mkdir();shutil.copyfile(root/'licenses/imgui.txt',tmp_path/'licenses/imgui.txt')
    inventory=json.loads((root/'licenses/desktop-dependencies.json').read_bytes());entry=inventory['dependencies']['imgui']
    if change=='noncommercial':entry['commercial_use']=False
    elif change=='manifest_hash':entry['vendor_manifest_sha256']='0'*64
    elif change in ('archive','traversal'):
        manifest=json.loads((vendor/'SOURCE.json').read_bytes())
        if change=='archive':manifest['archive_sha512']='0'*128
        else:manifest['files_sha256']['../../outside.cpp']='0'*64
        raw=(json.dumps(manifest,indent=2)+'\n').encode('utf-8');(vendor/'SOURCE.json').write_bytes(raw)
        entry['vendor_manifest_sha256']=hashlib.sha256(raw).hexdigest()
    elif change=='changed_header':(vendor/'imgui.h').write_bytes((vendor/'imgui.h').read_bytes()+b'\n// unreviewed\n')
    elif change=='missing_license':(vendor/'LICENSE.txt').unlink()
    elif change=='extra_source':(vendor/'extra.cpp').write_text('// unreviewed\n',encoding='utf-8')
    (tmp_path/'licenses/desktop-dependencies.json').write_text(json.dumps(inventory),encoding='utf-8')
    monkeypatch.setattr(gate,'ROOT',tmp_path)
    assert bool(gate.check_imgui())==(change!='none')
