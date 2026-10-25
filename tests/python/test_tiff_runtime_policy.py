"""Exact optional TIFF closure and fail-closed build/delivery checks."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import pytest

ROOT=Path(__file__).resolve().parents[2]
CMAKE=shutil.which('cmake') or 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'


def gate(root):
    spec=importlib.util.spec_from_file_location('tiff_policy',ROOT/'scripts/check_licenses.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.ROOT=root
    return module


@pytest.fixture
def policy(tmp_path):
    destination=tmp_path/'policy';(destination/'licenses').mkdir(parents=True);(destination/'cmake').mkdir()
    record=json.loads((ROOT/'licenses/io-dependencies.json').read_bytes())
    for name in ['io-dependencies.json',*[n for d in record['dependencies'].values() for n in d['notices']]]:
        shutil.copyfile(ROOT/'licenses'/name,destination/'licenses'/name)
    shutil.copyfile(ROOT/'cmake/TiffRuntimeManifest.cmake',destination/'cmake/TiffRuntimeManifest.cmake')
    return destination,record


@pytest.mark.parametrize('change',['none','noncommercial','version','missing_dependency','runtime_identity','extra_dll','notice','cmake','profile'])
def test_reviewed_closure_rejects_restricted_or_changed_inputs(policy,change):
    root,record=policy
    if change=='noncommercial':record['dependencies']['liblzma']['selected_license']='CC-BY-NC-4.0'
    elif change=='version':record['dependencies']['tiff']['tested_version']='4.7.2'
    elif change=='missing_dependency':del record['dependencies']['liblzma']
    elif change=='runtime_identity':record['files'][0]['sha256']='0'*64
    elif change=='extra_dll':record['files'].append({'path':'bin/turbojpeg.dll','kind':'runtime','bytes':1,'sha256':'0'*64})
    elif change=='notice':(root/'licenses/liblzma.txt').write_text('changed')
    elif change=='cmake':(root/'cmake/TiffRuntimeManifest.cmake').write_text('changed')
    elif change=='profile':record['profile']='other'
    (root/'licenses/io-dependencies.json').write_text(json.dumps(record))
    errors=gate(root).check_tiff_runtime()
    assert bool(errors)==(change!='none')


@pytest.fixture
def prefix(tmp_path):
    record=json.loads((ROOT/'licenses/io-dependencies.json').read_bytes());prefix=tmp_path/'codec'
    configured=os.environ.get('IMAGEJXX_REVIEWED_WINDOWS_TIFF_PREFIX')
    if configured is not None and not configured:
        pytest.fail('Configured reviewed Windows TIFF prefix must not be empty')
    origin=Path(configured) if configured is not None else ROOT/'.tools/imgui-sdk'
    verified=[]
    for row in record['files']:
        source=origin/row['path']
        if not source.is_file():
            if configured is not None:
                pytest.fail(f'Configured reviewed Windows TIFF artifact is unavailable: {source}')
            pytest.skip('Exact reviewed Windows SDK artifacts are not available')
        raw=source.read_bytes()
        if len(raw)!=row['bytes'] or hashlib.sha256(raw).hexdigest()!=row['sha256']:
            pytest.fail(f'Reviewed Windows TIFF artifact identity differs: {source}')
        verified.append((source,row['path']))
    # Validate the whole original manifest before copying any fixture member.
    for source,relative in verified:
        path=prefix/relative;path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,path)
    return prefix


def configure(directory,prefix,*,wrong_headers=False,foreign_zlib=False):
    project=directory/'project';project.mkdir()
    text='cmake_minimum_required(VERSION 3.18)\nproject(tiff_boundary NONE)\n'
    text+='add_library(TIFF::TIFF UNKNOWN IMPORTED)\n'
    text+=f'set_target_properties(TIFF::TIFF PROPERTIES IMPORTED_LOCATION_RELEASE "{prefix.as_posix()}/lib/tiff.lib" INTERFACE_INCLUDE_DIRECTORIES "{prefix.as_posix()}/'+('wrong' if wrong_headers else 'include')+'")\n'
    if foreign_zlib:
        (directory/'foreign.lib').write_bytes(b'foreign')
        text+=f'add_library(ZLIB::ZLIB UNKNOWN IMPORTED)\nset_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION_RELEASE "{(directory/"foreign.lib").as_posix()}")\n'
    text+=f'include("{(ROOT/"cmake/ReviewedTiffRuntime.cmake").as_posix()}")\nimagejxx_checked_tiff_runtime(runtime)\nfile(WRITE "${{CMAKE_BINARY_DIR}}/runtime.txt" "${{runtime}}")\n'
    text+='install(SCRIPT "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}")\ninstall(FILES ${runtime} DESTINATION bin)\n'
    (project/'CMakeLists.txt').write_text(text)
    result=subprocess.run([CMAKE,'-S',str(project),'-B',str(directory/'build')],capture_output=True,text=True,timeout=30)
    return result,directory/'build'


@pytest.mark.skipif(sys.platform!='win32',reason='The exact reviewed delivery profile is Windows x64')
@pytest.mark.parametrize('change',['none','missing_dll','changed_dll','changed_header','missing_link','wrong_headers','foreign_zlib'])
def test_cmake_fails_closed_for_runtime_header_and_linkage_changes(tmp_path,prefix,change):
    if change=='missing_dll':(prefix/'bin/liblzma.dll').unlink()
    elif change=='changed_dll':(prefix/'bin/zlib1.dll').write_bytes(b'changed')
    elif change=='changed_header':(prefix/'include/tiffio.h').write_text('changed')
    elif change=='missing_link':(prefix/'lib/tiff.lib').unlink()
    result,build=configure(tmp_path,prefix,wrong_headers=change=='wrong_headers',foreign_zlib=change=='foreign_zlib')
    assert (result.returncode==0)==(change=='none'),result.stdout+result.stderr
    if change=='none':
        paths=(build/'runtime.txt').read_text().split(';')
        assert {Path(p).name for p in paths}=={'tiff.dll','jpeg62.dll','liblzma.dll','zlib1.dll'}
        assert gate(ROOT).check_tiff_runtime(prefix)==[]


@pytest.mark.skipif(sys.platform!='win32',reason='The exact reviewed delivery profile is Windows x64')
@pytest.mark.parametrize('artifact',['bin/tiff.dll','include/tiffio.h'])
def test_recheck_refuses_changes_after_successful_configuration(tmp_path,prefix,artifact):
    result,build=configure(tmp_path,prefix);assert result.returncode==0,result.stdout+result.stderr
    (prefix/artifact).write_bytes(b'changed after configure')
    result=subprocess.run([CMAKE,'-P',str(build/'imagejxx-tiff-runtime-verify.cmake')],capture_output=True,text=True,timeout=30)
    assert result.returncode!=0 and 'Reviewed TIFF dependency changed' in result.stderr
    assert gate(ROOT).check_tiff_runtime(prefix)


@pytest.mark.skipif(sys.platform!='win32',reason='The exact reviewed delivery profile is Windows x64')
def test_install_refuses_a_changed_dependency_before_copying_any_codec(tmp_path,prefix):
    result,build=configure(tmp_path,prefix);assert result.returncode==0,result.stdout+result.stderr
    (prefix/'bin/liblzma.dll').write_bytes(b'changed between configure and install')
    destination=tmp_path/'delivery'
    result=subprocess.run([CMAKE,'--install',str(build),'--prefix',str(destination)],capture_output=True,text=True,timeout=30)
    assert result.returncode!=0 and 'Reviewed TIFF dependency changed' in result.stderr
    assert not list(destination.rglob('*.dll'))
