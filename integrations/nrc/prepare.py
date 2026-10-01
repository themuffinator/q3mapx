#!/usr/bin/env python3
"""Prepare the pinned NRC editor plus q3mapx authoring integration.

The source checkout is read-only. Output must be an empty project-local build
directory; this tool neither installs an editor nor changes an existing profile.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile


PROJECT = Path(__file__).resolve().parents[2]
INTEGRATION = Path(__file__).resolve().parent


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked_path(root, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or not relative.parts or any(p in ('..', '.') or ':' in p or '\\' in p for p in relative.parts):
        raise ValueError(f'Unsafe relative path: {name!r}')
    target = root.joinpath(*relative.parts)
    if not target.resolve().is_relative_to(root):
        raise ValueError(f'Path escapes output: {name!r}')
    return target


def normalized_file(path):
    return path.read_text(encoding='utf-8').encode('utf-8')


def prepare(upstream, output):
    manifest = json.loads((INTEGRATION/'manifest.json').read_text(encoding='utf-8'))
    patch = INTEGRATION/'q3mapx-authoring.patch'
    if digest(patch.read_bytes()) != manifest['patch_sha256']:
        raise ValueError('Patch does not match the integration manifest')
    # Validate additions before creating any output.
    additions = []
    for item in manifest['additions']:
        data = normalized_file(checked_path(PROJECT, item['source']))
        if digest(data) != item['sha256']:
            raise ValueError(f"Overlay does not match manifest: {item['source']}")
        additions.append((item['destination'], data))
    original_output = output.absolute()
    output = output.resolve()
    if not any(output.is_relative_to(base) and output != base for base in (PROJECT/'build', PROJECT/'.agents/tmp')):
        raise ValueError('Choose a dedicated output inside this project: build/ or .agents/tmp/')
    for part in (original_output, *original_output.parents):
        if part.is_symlink() or (hasattr(part, 'is_junction') and part.is_junction()):
            raise ValueError(f'Output must not traverse a link: {part}')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Output must be absent or empty; existing files are never replaced')
    upstream = upstream.resolve(strict=True)
    revision = manifest['upstream']['revision']
    actual = subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', '--verify', revision+'^{commit}'], text=True).strip()
    if actual != revision:
        raise ValueError('Pinned upstream commit is unavailable')
    output.mkdir(parents=True, exist_ok=True)
    archive = output/'.q3mapx-upstream.tar'
    with archive.open('xb') as destination:
        subprocess.run(['git', '-C', str(upstream), 'archive', '--format=tar', revision], stdout=destination, check=True)
    # Reject all links/special files. The pinned archive contains only files and
    # directories. Extract manually to make containment identical on Windows/Linux.
    with tarfile.open(archive) as source:
        members = source.getmembers()
        if len(members) > 20000 or sum(m.size for m in members) > 512*1024*1024:
            raise ValueError('Unexpected upstream archive size')
        seen = set()
        for member in members:
            target = checked_path(output, member.name)
            if target in seen or not (member.isfile() or member.isdir()):
                raise ValueError(f'Unsupported archive entry: {member.name}')
            seen.add(target)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as data, target.open('xb') as destination:
                    shutil.copyfileobj(data, destination)
                target.chmod(member.mode & 0o777)
    archive.unlink()  # exact disposable file created above; no recursive cleanup
    for item in manifest['edits']:
        target = checked_path(output, item['path'])
        if digest(target.read_bytes()) != item['original_sha256']:
            raise ValueError(f"Upstream hash mismatch: {item['path']}")
        # The pinned archive mixes LF/CRLF. Normalize only edited text, after
        # verifying its exact upstream bytes, for a compact portable patch.
        target.write_bytes(normalized_file(target))
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(output.parent))
    env.pop('GIT_DIR', None)
    env.pop('GIT_WORK_TREE', None)
    # Stop repository discovery at the output's parent: never apply to q3mapx's
    # own working tree, even though the prepared source is below it.
    for options in (['--check'], []):
        subprocess.run(['git', '-c', 'core.autocrlf=false', 'apply', *options, str(patch)], cwd=output, env=env, check=True)
    for item in manifest['edits']:
        target = checked_path(output, item['path'])
        # A user's global Git attributes may still request CRLF independently
        # of core.autocrlf. The integration hashes use explicit LF text.
        target.write_bytes(normalized_file(target))
        if digest(target.read_bytes()) != item['patched_sha256']:
            raise ValueError(f"Patched hash mismatch: {item['path']}")
    for name, data in additions:
        target = checked_path(output, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as destination:
            destination.write(data)
    (output/'q3mapx-authoring-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(f'Prepared NRC {revision} with q3mapx authoring in {output}')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, required=True, help='local NRC Git checkout containing the pinned commit')
    parser.add_argument('--output', type=Path, default=PROJECT/'build/nrc-authoring', help='empty project-local output directory')
    args = parser.parse_args()
    try:
        prepare(args.upstream, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        parser.exit(1, f'NRC preparation failed: {error}\n')
