"""Stage tested release assets. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

from release import ROOT, check, command, digest


def windows(output, version, sha):
    name = f'q3mapx-{version}-windows-x64'
    package = ROOT / 'build/package' / name
    manifest = json.loads((package / 'runtime-manifest.json').read_text(encoding='utf-8'))
    if (manifest['version'] != version or manifest['revision'] != sha
            or manifest['dirty_source_snapshot'] or not manifest['dependency_sources_downloaded']):
        raise ValueError('Windows package must have clean matching source and downloaded dependency sources')
    sources = {}
    for dependency in manifest['packages']:
        path = ROOT / 'build/package/dependency-sources' / dependency['source_archive']
        if digest(path) != dependency['source_sha256']:
            raise ValueError(f'Dependency source hash mismatch: {path}')
        sources[path.name] = path
    # Source packages are already compressed; store them without recompression.
    with zipfile.ZipFile(output / f'q3mapx-{version}-windows-dependency-sources.zip', 'x',
                         compression=zipfile.ZIP_STORED) as archive:
        archive.write(package / 'runtime-manifest.json', 'runtime-manifest.json')
        archive.write(package / 'RUNTIME-CREDITS.md', 'RUNTIME-CREDITS.md')
        for name, path in sorted(sources.items()):
            archive.write(path, 'dependency-sources/' + name)
    binary_archive = package.parent / (package.name + '.zip')
    shutil.copy2(binary_archive, output / binary_archive.name)


def linux(output, version, sha):
    name = f'q3mapx-{version}-ubuntu-24.04-x64'
    package = ROOT / 'build/package' / name
    if package.exists():
        raise ValueError(f'Refusing to overwrite {package}')
    subprocess.run(['cmake', '--install', 'build/release', '--prefix', str(package)], cwd=ROOT, check=True)
    for item in ('README.md', 'CHANGELOG.md', 'VERSION', 'COPYING', 'GPL', 'LGPL', 'LICENSE', 'CONTRIBUTORS'):
        shutil.copy2(ROOT / item, package / item)
    shutil.copytree(ROOT / 'docs', package / 'docs')
    env = os.environ.copy()
    env['QT_QPA_PLATFORM'] = 'offscreen'
    for product in ('q3mapx', 'q3mapx-workbench'):
        result = subprocess.check_output([str(package / 'bin' / product), '--version'], env=env, text=True).strip()
        if result != f'{product} {version}' and not result.startswith(f'{product} {version} (NRC '):
            raise ValueError(f'Packaged version mismatch: {result}')
    libraries = command('ldd', package / 'bin/q3mapx', package / 'bin/q3mapx-workbench')
    if 'not found' in libraries:
        raise ValueError('Missing Linux runtime library')
    manifest = {'schema_version': 1, 'version': version, 'revision': sha,
                'platform': 'Ubuntu 24.04 x64', 'runtime': 'system libraries; see docs/RELEASE.md',
                'ldd': libraries,
                'files': [{'path': p.relative_to(package).as_posix(), 'sha256': digest(p)}
                          for p in sorted(package.rglob('*')) if p.is_file()]}
    (package / 'runtime-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    epoch = int(command('git', 'show', '-s', '--format=%ct', 'HEAD'))

    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ''
        info.mtime = epoch
        return info

    archive_path = output / (name + '.tar.gz')
    with tarfile.open(archive_path, 'w:gz') as archive:
        archive.add(package, arcname=name, filter=normalize)
    # Smoke test an extracted archive, not only the build tree.
    extracted = ROOT / 'build/package-validation-linux'
    extracted.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive_path) as archive:
        archive.extractall(extracted, filter='data')
    binary = extracted / name / 'bin/q3mapx'
    subprocess.run(['python3', str(ROOT / 'tests/integration.py'), '--compiler', str(binary),
                    '--work-dir', str(extracted / 'pipeline')], cwd=ROOT, check=True)
    gui = extracted / name / 'bin/q3mapx-workbench'
    subprocess.run([str(gui), '-platform', 'offscreen', '--state-dir', str(extracted / 'ui-state'),
                    '--render-preview', str(extracted / 'workbench.png')], cwd=ROOT, env=env, check=True)
    if (extracted / 'workbench.png').stat().st_size < 10000:
        raise ValueError('Packaged workbench did not render its offscreen preview')
    subprocess.run(['git', 'archive', '--format=zip', f'--prefix=q3mapx-{version}/',
                    '-o', str(output / f'q3mapx-{version}-source.zip'), 'HEAD'], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=('windows', 'linux'))
    args = parser.parse_args()
    version = check()
    sha = command('git', 'rev-parse', 'HEAD')
    if command('git', 'status', '--porcelain'):
        raise ValueError('Commit the release source before packaging')
    output = ROOT / 'build/release-assets'
    output.mkdir(parents=True, exist_ok=False)
    {'windows': windows, 'linux': linux}[args.platform](output, version, sha)


if __name__ == '__main__':
    main()
