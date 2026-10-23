"""Capture unmodified ImageJ 1.54r RoiEnlarger results, never imagejxx outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--imagej-root', type=Path, required=True, help='Retained root containing sources/ and classes/')
    parser.add_argument('--jdk', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()
    root, jdk, output, fixture = [p.resolve() for p in (args.imagej_root, args.jdk, args.output_dir, args.fixture)]
    if output.exists() or fixture.exists():
        raise ValueError('Capture directory and fixture must be fresh')
    output.mkdir(parents=True)
    classes = output/'classes'
    classes.mkdir()
    source = Path(__file__).resolve().parent/'SelectionEnlargementReference.java'
    suffix = '.exe' if (jdk/'bin/java.exe').exists() else ''
    java, javac = (jdk/f'bin/{name}{suffix}' for name in ('java', 'javac'))
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    inputs = [source, java, javac, root/'sources/LICENSE.txt',
              *sorted((root/'sources').rglob('*.java')),
              *sorted((root/'classes').rglob('*.class'))]
    if any(p.is_symlink() or not p.is_file() for p in inputs):
        raise ValueError('Reference inputs must be ordinary files')
    pins = {str(p): sha(p) for p in inputs}
    commands = [[str(javac), '-encoding', 'UTF-8', '-cp', str(root/'classes'), '-d', str(classes), str(source)],
                [str(java), '-Djava.awt.headless=true', '-Duser.home='+str(output/'home001'),
                 '-Djava.io.tmpdir='+str(output/'temp001'), '-cp', os.pathsep.join(map(str, (classes, root/'classes'))),
                 'SelectionEnlargementReference']]
    for name, command in zip(('compile', 'execute'), commands):
        with (output/(name+'.stdout')).open('xb') as out, (output/(name+'.stderr')).open('xb') as err:
            subprocess.run(command, stdout=out, stderr=err, check=True, timeout=180)
    raw = (output/'execute.stdout').read_bytes()
    if len(raw) > 4*1024*1024:
        raise ValueError('Reference output exceeds bound')
    data = json.loads(raw)
    assert data['schema'] == 'imagejxx.reference.selection-enlargement' and data['imagej_version'] == '1.54r'
    assert len(data['cases']) == len({(r['kind'], r['distance']) for r in data['cases']}) == 144
    for row in data['cases']:
        for key in ('before', 'after'):
            assert len(row[key]) == 40 and all(len(v) == 48 and set(v) <= {'0', '1'} for v in row[key])
    assert all(sha(Path(n)) == h for n, h in pins.items())
    fixture.write_text(json.dumps(data, indent=2)+'\n', encoding='utf-8')
    (output/'receipt.json').write_text(json.dumps(dict(state='passed', cases=144, commands=commands, inputs=pins,
        fixture_sha256=sha(fixture), raw_sha256=sha(output/'execute.stdout')), indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
