"""Verify a transferred source-built dependency prefix, without executing it.

This explicitly selected preparation protocol leaves the historical fixed
Windows runtime policy unchanged. Integrity is not hosted/product acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import stat
import types

CERTIFICATE = 'share/imagejxx-ci-build.json'
PROFILES = {
    'windows-x64-ci-dynamic-source-001': ('win32', 'x86_64', 'shared'),
    'macos-arm64-ci-static-source-001': ('darwin', 'arm64', 'static'),
    'macos-x86_64-ci-static-source-001': ('darwin', 'x86_64', 'static'),
}
WINDOWS_LIBRARIES = {
    'zlib': {'link': 'lib/zlib.lib', 'runtime': 'bin/zlib1.dll'},
    'jpeg': {'link': 'lib/jpeg.lib', 'runtime': 'bin/jpeg62.dll'},
    'lzma': {'link': 'lib/lzma.lib', 'runtime': 'bin/liblzma.dll'},
    'png': {'link': 'lib/libpng16.lib', 'runtime': 'bin/libpng16.dll'},
    'glfw': {'link': 'lib/glfw3dll.lib', 'runtime': 'bin/glfw3.dll'},
    'tiff': {'link': 'lib/tiff.lib', 'runtime': 'bin/tiff.dll'},
}
STATIC_LIBRARIES = {name: {'link': 'lib/lib'+library+'.a'} for name,library in
                    (('zlib','z'),('jpeg','jpeg'),('lzma','lzma'),('png','png16'),('glfw','glfw3'),('tiff','tiff'))}
DEPENDENCIES = ('zlib','libjpeg-turbo','liblzma','png','glfw','tiff')
COMPILER_FIELDS = {'system','pointer_bytes','c_id','c_version','c','cxx_id',
                   'cxx_version','cxx','architecture','sysroot'}


def regular(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or getattr(info,'st_file_attributes',0) & 0x400:
        raise ValueError('Dependency member is not an unlinked regular file: '+str(path))


def pin(path):
    regular(path)
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def member(prefix, name):
    if (not isinstance(name,str) or not name or '\\' in name
            or any(c in name for c in '\x00\r\n;"$:') or PurePosixPath(name).is_absolute()
            or any(part in ('','.','..') for part in name.split('/'))):
        raise ValueError('Invalid dependency member path')
    path = prefix.joinpath(*name.split('/'))
    for ancestor in (path,*path.parents):
        if ancestor == prefix.parent: break
        info = ancestor.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0) & 0x400:
            raise ValueError('Linked dependency member/ancestor')
    if not path.resolve(strict=True).is_relative_to(prefix):
        raise ValueError('Dependency member escapes prefix')
    return path


def same_spelling(left, right):
    return isinstance(left,str) and isinstance(right,str) and left.replace('\\','/') == right.replace('\\','/')


def declared_identity(row, *, path_key, system):
    """Validate a producer identity without executing/requiring its old tool."""
    if (not isinstance(row,dict) or set(row)!={path_key,'bytes','sha256'}
            or type(row['bytes']) is not int or row['bytes']<=0
            or not isinstance(row['sha256'],str) or re.fullmatch('[0-9a-f]{64}',row['sha256']) is None):
        raise ValueError('Invalid recorded producer tool identity')
    producer_path(row[path_key],system)


def producer_path(value, system):
    if not isinstance(value,str) or not value or any(c in value for c in '\x00\r\n;"'):
        raise ValueError('Invalid original producer path')
    path = PureWindowsPath(value) if system=='win32' else PurePosixPath(value)
    if not path.is_absolute() or '..' in path.parts:
        raise ValueError('Producer path must be absolute in its declared platform syntax')
    return path


def evidence(prefix, inventory, row, relative, *, maximum=4*1024*1024):
    if (not isinstance(row,dict) or set(row)!={'path','bytes','sha256'} or row['path']!=relative
            or type(row['bytes']) is not int or not 0<row['bytes']<=maximum):
        raise ValueError('Invalid copied toolchain/configuration evidence')
    listed=inventory.get(relative)
    if (listed is None or listed['kind']!='metadata'
            or any(row[key]!=listed[key] for key in ('bytes','sha256'))):
        raise ValueError('Effective cache evidence changed' if 'CMakeCache.txt' in relative
                         else 'Copied evidence identity differs from member inventory')
    path=member(prefix,relative)
    if pin(path)!={key:row[key] for key in ('bytes','sha256')}:
        raise ValueError('Copied toolchain/configuration evidence changed')
    return path


def trusted_recipe(builder, prefix):
    """Only the caller-supplied trusted repository/installed script is loaded.

    Never import the transferred prefix's copy or run its CLI. Compiling this
    reviewed local helper directly avoids creating __pycache__ in an SDK.
    This binds the closed recipe; it does not authenticate a claimed build.
    """
    path=Path(builder).resolve(strict=True)
    if path.is_relative_to(prefix):
        raise ValueError('Trusted recipe must be outside the transferred prefix')
    regular(path)
    module=types.ModuleType('_imagejxx_trusted_source_recipe')
    module.__file__=str(path)
    exec(compile(path.read_bytes(),str(path),'exec'),module.__dict__)
    return module


def check_configuration(prefix, certificate, inventory, builder):
    configuration=certificate['configuration']
    profile=certificate['profile']
    system,architecture,_=PROFILES[profile]
    recipe=trusted_recipe(builder,prefix)
    toolchain=configuration.get('toolchain')
    tools={'cmake','ninja'}
    if architecture=='x86_64':tools.add('nasm')
    if system=='darwin':tools.add('xcrun')
    expected=tools|{'python','compiler'}|({'sdk'} if system=='darwin' else set())
    if not isinstance(toolchain,dict) or set(toolchain)!=expected:
        raise ValueError('Missing or unexpected producer toolchain evidence')
    python=toolchain['python']
    if (not isinstance(python,dict) or set(python)!={'version','path','bytes','sha256'}
            or not isinstance(python['version'],str) or not python['version'].strip()):
        raise ValueError('Invalid recorded Python toolchain identity')
    declared_identity({key:python[key] for key in ('path','bytes','sha256')},path_key='path',system=system)
    for name in sorted(tools):
        tool=toolchain[name]
        if not isinstance(tool,dict) or set(tool)!={'path','bytes','sha256','version_evidence'}:
            raise ValueError('Invalid recorded build tool schema')
        declared_identity({key:tool[key] for key in ('path','bytes','sha256')},path_key='path',system=system)
        path=evidence(prefix,inventory,tool['version_evidence'],
                      'share/imagejxx-ci/'+name+'-version.txt',maximum=65536)
        if not path.read_text(encoding='utf-8').strip():
            raise ValueError('Empty producer tool version evidence')
    compiler=toolchain['compiler']
    if (not isinstance(compiler,dict)
            or set(compiler)!=COMPILER_FIELDS|{'metadata','cache','c_identity','cxx_identity'}
            or any(not isinstance(compiler[key],str) for key in COMPILER_FIELDS)):
        raise ValueError('Invalid compiler evidence schema')
    metadata=evidence(prefix,inventory,compiler['metadata'],'share/imagejxx-ci/compiler.txt',maximum=65536)
    observed={}
    for line in metadata.read_text(encoding='utf-8').splitlines():
        if '=' not in line:raise ValueError('Malformed compiler metadata evidence')
        key,value=line.split('=',1)
        if key not in COMPILER_FIELDS or key in observed:raise ValueError('Unknown/duplicate compiler evidence field')
        observed[key]=value
    if set(observed)!=COMPILER_FIELDS or any(not same_spelling(observed[key],compiler[key]) for key in observed):
        raise ValueError('Declared compiler differs from copied compiler evidence')
    wanted_system,wanted_id=('Windows','MSVC') if system=='win32' else ('Darwin','AppleClang')
    if (compiler['system']!=wanted_system or compiler['pointer_bytes']!='8'
            or compiler['c_id']!=wanted_id or compiler['cxx_id']!=wanted_id
            or not compiler['c_version'] or not compiler['cxx_version']
            or (system=='win32' and compiler['architecture'] not in ('x64','X64','AMD64','x86_64'))):
        raise ValueError('Compiler evidence does not match the native source profile')
    for language in ('c','cxx'):
        producer_path(compiler[language],system)
        declared_identity(compiler[language+'_identity'],path_key='resolved_path',system=system)
    dependencies=configuration.get('dependencies')
    if (not isinstance(dependencies,dict) or set(dependencies)!=set(DEPENDENCIES)
            or any(not isinstance(row,dict) for row in dependencies.values())):
        raise ValueError('Prepared dependency configuration membership changed')
    first=dependencies['zlib'].get('requested')
    if not isinstance(first,dict) or 'CMAKE_INSTALL_PREFIX' not in first:
        raise ValueError('Missing original source-build prefix')
    original_prefix=producer_path(first['CMAKE_INSTALL_PREFIX'],system)
    selected=recipe.profile_definition(profile)
    common=recipe.common_definitions(selected,original_prefix)
    common['CMAKE_MAKE_PROGRAM']=toolchain['ninja']['path']
    if system=='darwin':
        sdk=toolchain['sdk']
        if (not isinstance(sdk,dict) or set(sdk)!={'path','version','settings'}
                or not isinstance(sdk['version'],str) or not sdk['version'].strip()):
            raise ValueError('Invalid macOS SDK evidence')
        producer_path(sdk['path'],system)
        settings=evidence(prefix,inventory,sdk['settings'],'share/imagejxx-ci/SDKSettings.json')
        value=json.loads(settings.read_bytes())
        if not isinstance(value,dict) or not value:raise ValueError('Invalid copied macOS SDK settings')
        if not same_spelling(compiler['sysroot'],sdk['path']):
            raise ValueError('Compiler sysroot differs from the recorded macOS SDK')
        common['CMAKE_OSX_SYSROOT']=sdk['path']
    elif compiler['sysroot']:
        raise ValueError('Windows compiler unexpectedly declares a macOS SDK')
    compiler_cache=evidence(prefix,inventory,compiler['cache'],'share/imagejxx-ci/compiler-CMakeCache.txt')
    recipe.check_effective(recipe.cache_values(compiler_cache),{
        **common,'CMAKE_C_COMPILER':compiler['c'],'CMAKE_CXX_COMPILER':compiler['cxx']})
    options=recipe.dependency_definitions(selected,original_prefix,
        toolchain['nasm']['path'] if 'nasm' in tools else None)
    for name in DEPENDENCIES:
        row=dependencies[name]
        if not isinstance(row,dict) or set(row)!={'requested','effective','cache'}:
            raise ValueError('Invalid dependency configuration schema')
        required={**common,**options[name]}
        requested,effective=row['requested'],row['effective']
        if (not isinstance(requested,dict) or not isinstance(effective,dict)
                or set(requested)!=set(required) or set(effective)!=set(required)
                or any(not same_spelling(requested[key],value)
                       or not same_spelling(effective[key],value) for key,value in required.items())):
            raise ValueError('Effective dependency configuration differs from the fixed source recipe')
        path=evidence(prefix,inventory,row['cache'],
                      'share/imagejxx-ci/configuration/'+name+'.CMakeCache.txt')
        recipe.check_effective(recipe.cache_values(path),required)


def validate_prefix(prefix, profile, *, catalog, builder):
    if profile not in PROFILES: raise ValueError('Unknown source dependency profile')
    prefix = Path(prefix)
    if prefix.is_symlink() or getattr(prefix.lstat(),'st_file_attributes',0) & 0x400:
        raise ValueError('Dependency prefix must not be linked')
    prefix = prefix.resolve(strict=True)
    certificate = json.loads(member(prefix,CERTIFICATE).read_bytes())
    if (certificate.get('schema') != 'imagejxx.ci-platform-dependencies.001'
            or certificate.get('state') != 'prepared' or certificate.get('acceptance') is not False
            or certificate.get('profile') != profile):
        raise ValueError('Incomplete/wrong source preparation certificate')
    catalog = Path(catalog);builder = Path(builder)
    catalog_pin = pin(catalog); data = json.loads(catalog.read_bytes())
    if (certificate.get('source_manifest_path') != 'licenses/ci-linux-source-dependencies.json'
            or certificate.get('source_manifest_sha256') != catalog_pin['sha256']
            or certificate.get('sources') != data.get('dependencies')
            or set(data.get('dependencies',{})) != {'zlib','libjpeg-turbo','liblzma','png','glfw','tiff','stb'}
            or any(d.get('commercial_use') is not True for d in data['dependencies'].values())):
        raise ValueError('Source/notice catalog differs from reviewed inputs')
    configuration = certificate['configuration']
    system,architecture,linkage = PROFILES[profile]
    if configuration['platform'] != {'name':profile,'system':system,'architecture':architecture,
                                    'linkage':linkage,'deployment_target':'11.0' if system=='darwin' else None}:
        raise ValueError('Prepared platform does not match explicit profile')
    for key,expected,path in (('builder',pin(builder),'share/imagejxx-ci/ci_platform_dependencies.py'),
                              ('source_catalog',catalog_pin,'share/imagejxx-ci/source-catalog.json')):
        row = configuration[key]
        if row.get('path') != path or any(row.get(k)!=v for k,v in expected.items()) or pin(member(prefix,path))!=expected:
            raise ValueError('Prepared builder/catalog identity differs')
    libraries = WINDOWS_LIBRARIES if system=='win32' else STATIC_LIBRARIES
    if configuration['libraries'] != libraries: raise ValueError('Selected library roles changed')
    files = certificate['installed_files'];expected_files = {}
    if not isinstance(files,list) or not files: raise ValueError('Missing prepared member inventory')
    for row in files:
        if (not isinstance(row,dict) or set(row) != {'path','bytes','sha256','kind'}
                or row['path'] in expected_files or row['path']==CERTIFICATE
                or type(row['bytes']) is not int or row['bytes']<0
                or not isinstance(row['sha256'],str) or re.fullmatch('[0-9a-f]{64}',row['sha256']) is None
                or row['kind'] not in ('runtime','link','header','metadata')):
            raise ValueError('Invalid/duplicate dependency member identity')
        if pin(member(prefix,row['path'])) != {k:row[k] for k in ('bytes','sha256')}:
            raise ValueError('Prepared dependency member changed: '+row['path'])
        expected_files[row['path']] = row
    actual = set()
    def fail(error): raise error
    for parent,dirs,names in os.walk(prefix,followlinks=False,onerror=fail):
        for name in dirs:
            path = Path(parent)/name;info = path.lstat()
            if path.is_symlink() or getattr(info,'st_file_attributes',0)&0x400:raise ValueError('Linked dependency directory')
        for name in names:
            path=Path(parent)/name;regular(path);actual.add(path.relative_to(prefix).as_posix())
    if actual != set(expected_files)|{CERTIFICATE}:raise ValueError('Prepared dependency file closure changed')
    for roles in libraries.values():
        for kind,name in roles.items():
            if name not in expected_files or expected_files[name]['kind']!=kind:
                raise ValueError('Selected library has missing/wrong member role')
    if system=='darwin' and any(name.endswith(('.dll','.dylib')) for name in actual):
        raise ValueError('Static profile contains shared runtime libraries')
    check_configuration(prefix,certificate,expected_files,builder)
    commands=certificate.get('commands')
    if not commands or any(row.get('state')!='passed' or row.get('exit_code')!=0 for row in commands):
        raise ValueError('Dependency preparation commands are incomplete')
    return certificate


def quote(value):
    return '"'+str(value).replace('\\','/').replace('$','\\$').replace(';','\\;').replace('"','\\"')+'"'


def emit_cmake(prefix, certificate, output, *, tiff_link):
    prefix=Path(prefix).resolve(strict=True);output=Path(output)
    if Path(tiff_link).resolve(strict=True) != member(prefix,certificate['configuration']['libraries']['tiff']['link']):
        raise ValueError('TIFF imported target is outside the prepared profile')
    # The SDK installs the entire prepared prefix. Recheck its exact closure,
    # not just the original members, before that copy can occur. Inspect each
    # directory before descending, and inspect ancestors root-first so a link
    # replacing a parent after configuration is never traversed by this walk.
    checks=[]
    for directory in (*reversed(prefix.parents),prefix):
        path=quote(directory)
        checks.extend([f'if(IS_SYMLINK {path})',
            ' message(FATAL_ERROR "Prepared dependency directory linked after configuration")','endif()',
            f'if(NOT IS_DIRECTORY {path})',
            ' message(FATAL_ERROR "Prepared dependency directory missing after configuration")','endif()'])
    expected=[row['path'] for row in certificate['installed_files']]+[CERTIFICATE]
    checks.extend([
        'set(_imagejxx_ci_directories '+quote(prefix)+')',
        'set(_imagejxx_ci_files)',
        'while(_imagejxx_ci_directories)',
        ' list(POP_BACK _imagejxx_ci_directories _imagejxx_ci_directory)',
        ' file(GLOB _imagejxx_ci_members LIST_DIRECTORIES true "${_imagejxx_ci_directory}/*")',
        ' foreach(_imagejxx_ci_member IN LISTS _imagejxx_ci_members)',
        '  if(IS_SYMLINK "${_imagejxx_ci_member}")',
        '   message(FATAL_ERROR "Prepared dependency member linked after configuration")',
        '  endif()',
        '  if(IS_DIRECTORY "${_imagejxx_ci_member}")',
        '   list(APPEND _imagejxx_ci_directories "${_imagejxx_ci_member}")',
        '  else()',
        '   file(RELATIVE_PATH _imagejxx_ci_relative '+quote(prefix)+' "${_imagejxx_ci_member}")',
        '   list(APPEND _imagejxx_ci_files "${_imagejxx_ci_relative}")',
        '  endif()',
        ' endforeach()',
        'endwhile()',
        'set(_imagejxx_ci_expected '+ ' '.join(quote(name) for name in expected)+')',
        'list(SORT _imagejxx_ci_files)',
        'list(SORT _imagejxx_ci_expected)',
        'if(NOT _imagejxx_ci_files STREQUAL _imagejxx_ci_expected)',
        ' message(FATAL_ERROR "Prepared dependency file closure changed after configuration")',
        'endif()'])
    for row in [*certificate['installed_files'],{'path':CERTIFICATE,**pin(prefix/CERTIFICATE)}]:
        path=quote(prefix/row['path']);digest=quote(row['sha256']);size=row['bytes']
        checks.extend([f'if(NOT EXISTS {path} OR IS_DIRECTORY {path} OR IS_SYMLINK {path})',
            ' message(FATAL_ERROR "Prepared dependency missing or linked")','endif()',
            f'file(SIZE {path} _bytes)',f'file(SHA256 {path} _sha)',
            f'if(NOT _bytes EQUAL {size} OR NOT _sha STREQUAL {digest})',
            ' message(FATAL_ERROR "Prepared dependency changed after configuration")','endif()'])
    verify=output.with_name(output.stem+'-verify.cmake')
    verify.write_bytes(('\n'.join(checks)+'\n').encode())
    libraries=certificate['configuration']['libraries']
    runtimes=[prefix/libraries[name]['runtime'] for name in ('tiff','jpeg','lzma','zlib') if 'runtime' in libraries[name]]
    lines=['set(IMAGEJXX_IO_RUNTIME_FILES '+ ' '.join(quote(p) for p in runtimes)+')',
           'set(IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT '+quote(verify)+')',
           'set(IMAGEJXX_CI_SOURCE_DEP_PREFIX '+quote(prefix)+')']
    output.write_bytes(('\n'.join(lines)+'\n').encode())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('prefix','catalog','builder','tiff-link','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--profile',required=True);args=parser.parse_args()
    certificate=validate_prefix(args.prefix,args.profile,catalog=args.catalog,builder=args.builder)
    emit_cmake(args.prefix,certificate,args.output,tiff_link=args.tiff_link)


if __name__=='__main__':main()
