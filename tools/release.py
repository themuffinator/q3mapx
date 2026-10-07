"""Prepare, validate and publish q3mapx releases. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from datetime import date, datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)'
REPOSITORY = 'https://github.com/themuffinator/q3mapx'


def version_tuple(value):
    if not re.fullmatch(VERSION_PATTERN, value):
        raise ValueError('Use MAJOR.MINOR.PATCH without a v prefix or leading zeros')
    return tuple(map(int, value.split('.')))


def read_version(root=ROOT):
    raw = (root / 'VERSION').read_text(encoding='utf-8')
    value = raw.strip()
    version_tuple(value)
    if raw not in (value, value + '\n'):
        raise ValueError('VERSION must contain exactly one version line')
    return value


def sections(root=ROOT):
    text = (root / 'CHANGELOG.md').read_text(encoding='utf-8')
    headings = list(re.finditer(r'^## \[([^\]]+)\](.*)$', text, re.M))
    if not headings or headings[0].group(1) != 'Unreleased':
        raise ValueError('CHANGELOG.md must start with an [Unreleased] section')
    result = {}
    previous = None
    for index, heading in enumerate(headings):
        name, suffix = heading.group(1, 2)
        if name in result:
            raise ValueError(f'Duplicate changelog section: {name}')
        if name == 'Unreleased':
            if suffix.strip():
                raise ValueError('Unreleased must not have a date')
        else:
            current = version_tuple(name)
            if previous is not None and current >= previous:
                raise ValueError('Changelog releases must be newest first')
            previous = current
            if not re.fullmatch(r' - \d{4}-\d{2}-\d{2}', suffix):
                raise ValueError(f'Missing ISO release date for {name}')
            date.fromisoformat(suffix[3:])
        end = headings[index + 1].start() if index + 1 < len(headings) else len(text)
        body = text[heading.end():end]
        # Reference links at the end are navigation, not release notes.
        body = re.split(r'^\[[^\]]+\]: ', body, maxsplit=1, flags=re.M)[0].strip()
        result[name] = body
    return result


def check(root=ROOT, expected=None):
    version = read_version(root)
    entries = sections(root)
    if list(entries)[1:2] != [version] or not re.search(r'^- \S', entries.get(version, ''), re.M):
        raise ValueError('VERSION must match the newest dated, nonempty changelog entry')
    if expected is not None:
        version_tuple(expected)
        if expected != version:
            raise ValueError(f'Requested {expected}, but this commit contains {version}')
        if entries['Unreleased']:
            raise ValueError('Prepare the pending Unreleased notes before publishing')
    return version


def prepare(version, root=ROOT, release_date=None):
    current = check(root)
    if version_tuple(version) <= version_tuple(current):
        raise ValueError(f'New version must be greater than {current}')
    if not re.search(r'^- \S', sections(root)['Unreleased'], re.M):
        raise ValueError('Add user-visible notes under Unreleased first')
    stamp = release_date or datetime.now(timezone.utc).date().isoformat()
    date.fromisoformat(stamp)
    path = root / 'CHANGELOG.md'
    text = path.read_text(encoding='utf-8')
    text = text.replace('## [Unreleased]', f'## [Unreleased]\n\n## [{version}] - {stamp}', 1)
    text = re.sub(r'^\[Unreleased\]: .*$',
                  f'[Unreleased]: {REPOSITORY}/compare/v{version}...HEAD', text, flags=re.M)
    text = text.rstrip() + f'\n[{version}]: {REPOSITORY}/compare/v{current}...v{version}\n'
    path.write_text(text, encoding='utf-8')
    (root / 'VERSION').write_text(version + '\n', encoding='ascii')


def command(*args):
    return subprocess.check_output(list(map(str, args)), cwd=ROOT, text=True).strip()


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def api(endpoint, *args, missing_ok=False):
    result = subprocess.run(['gh', 'api', endpoint, *args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        if missing_ok and '(HTTP 404)' in result.stderr:
            return None
        raise RuntimeError(result.stderr.strip())
    return json.loads(result.stdout) if result.stdout else None


def remote_state(repo, tag, sha):
    release = api(f'repos/{repo}/releases/tags/{tag}', missing_ok=True)
    if release and not release['draft']:
        raise ValueError(f'{tag} is already published; published releases are never overwritten')
    ref = api(f'repos/{repo}/git/ref/tags/{tag}', missing_ok=True)
    if ref:
        commit = api(f'repos/{repo}/commits/{tag}')['sha']
        if commit != sha:
            raise ValueError(f'{tag} already points to another commit; tags are never moved')
    if release and release['target_commitish'] != sha:
        raise ValueError('Existing draft belongs to a different source commit')
    return release, ref


def asset_names(version):
    stem = f'q3mapx-{version}'
    return {f'{stem}-windows-x64.zip', f'{stem}-ubuntu-24.04-x64.tar.gz',
            f'{stem}-source.zip', f'{stem}-windows-dependency-sources.zip'}


def verify_assets(directory, version, sha):
    expected = asset_names(version)
    found = {p.name for p in directory.iterdir()}
    if found != expected | {'release-manifest.json', 'SHA256SUMS'}:
        raise ValueError(f'Unexpected asset set: {sorted(found ^ (expected | {"release-manifest.json", "SHA256SUMS"}))}')
    if any(p.is_symlink() or not p.is_file() for p in directory.iterdir()):
        raise ValueError('Release assets must be regular files')
    manifest = json.loads((directory / 'release-manifest.json').read_text(encoding='utf-8'))
    if manifest['version'] != version or manifest['revision'] != sha:
        raise ValueError('Asset manifest does not match release version/commit')
    if set(manifest['assets']) != expected:
        raise ValueError('Manifest asset list is incomplete')
    for name, item in manifest['assets'].items():
        path = directory / name
        if item != {'sha256': digest(path), 'size': path.stat().st_size}:
            raise ValueError(f'Asset integrity failure: {name}')
    checksums = ''.join(f'{digest(directory / name)}  {name}\n'
                        for name in sorted(expected | {'release-manifest.json'}))
    if (directory / 'SHA256SUMS').read_text(encoding='ascii') != checksums:
        raise ValueError('SHA256SUMS does not match the complete release')
    return manifest


def finalize(directory, version, sha):
    if {p.name for p in directory.iterdir()} != asset_names(version):
        raise ValueError('Cannot finalize an incomplete or unexpected asset set')
    assets = {}
    for path in sorted(directory.iterdir()):
        if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size < 2_000_000_000:
            raise ValueError(f'Invalid or oversized GitHub asset: {path.name}')
        assets[path.name] = {'sha256': digest(path), 'size': path.stat().st_size}
    manifest = {'schema_version': 1, 'version': version, 'revision': sha, 'assets': assets}
    (directory / 'release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (directory / 'SHA256SUMS').write_text(''.join(
        f'{digest(directory / name)}  {name}\n' for name in sorted(set(assets) | {'release-manifest.json'})), encoding='ascii')
    verify_assets(directory, version, sha)


def publish(directory, version, repo, prerelease=False):
    check(expected=version)
    sha = command('git', 'rev-parse', 'HEAD')
    if command('git', 'status', '--porcelain'):
        raise ValueError('Release checkout must be clean')
    verify_assets(directory, version, sha)
    tag = 'v' + version
    release, ref = remote_state(repo, tag, sha)
    if not ref:
        api(f'repos/{repo}/git/refs', '-X', 'POST', '-f', f'ref=refs/tags/{tag}', '-f', f'sha={sha}')
    notes = directory.parent / 'release-notes.md'
    body = sections()[version] + (
        f'\n\nSource: [`{sha[:12]}`]({REPOSITORY}/commit/{sha}).\n\n'
        'Download the Windows ZIP or Ubuntu archive below. The source ZIP contains the '
        'matching project source; Windows dependency sources are supplied separately. '
        'Verify downloads against `SHA256SUMS`. See '
        f'[installation and limitations]({REPOSITORY}/blob/{tag}/docs/RELEASE.md).\n')
    notes.write_text(body, encoding='utf-8')
    common = ['--repo', repo, '--title', f'q3mapx {version}', '--notes-file', str(notes),
              '--prerelease' if prerelease else '--prerelease=false']
    if release:
        command('gh', 'release', 'edit', tag, *common)
        # A failed upload can leave a draft. Only draft assets may be replaced.
        for asset in release['assets']:
            api(f'repos/{repo}/releases/assets/{asset["id"]}', '-X', 'DELETE')
    else:
        command('gh', 'release', 'create', tag, '--verify-tag', '--target', sha, '--draft', *common)
    for path in sorted(directory.iterdir()):
        command('gh', 'release', 'upload', tag, path, '--repo', repo)
    release = api(f'repos/{repo}/releases/tags/{tag}')
    if not release['draft'] or {a['name'] for a in release['assets']} != {p.name for p in directory.iterdir()}:
        raise ValueError('Draft does not contain the exact expected assets')
    for asset in release['assets']:
        local = directory / asset['name']
        if asset['size'] != local.stat().st_size or asset.get('digest') != 'sha256:' + digest(local):
            raise ValueError(f'Uploaded asset failed server-side verification: {asset["name"]}')
    # All assets are attached before publication, including on immutable-release repositories.
    command('gh', 'release', 'edit', tag, '--repo', repo, '--draft=false',
            '--latest=false' if prerelease else '--latest')
    published = api(f'repos/{repo}/releases/tags/{tag}')
    if published['draft'] or published['prerelease'] != prerelease:
        raise ValueError('GitHub did not publish the requested release state')
    print(published['html_url'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('prepare', help='Move Unreleased notes into a new dated release and update VERSION')
    p.add_argument('version')
    p.add_argument('--date')
    for name in ('check', 'preflight', 'finalize', 'verify', 'publish'):
        p = sub.add_parser(name)
        p.add_argument('--version', required=name != 'check')
        if name in ('preflight', 'publish'):
            p.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY', 'themuffinator/q3mapx'))
        if name in ('finalize', 'verify', 'publish'):
            p.add_argument('--assets', type=Path, default=ROOT / 'build/release-assets')
        if name == 'publish':
            p.add_argument('--prerelease', action='store_true')
    args = parser.parse_args()
    if args.action == 'prepare':
        prepare(args.version, release_date=args.date)
        print(f'Prepared {args.version}; review VERSION and CHANGELOG.md, then commit them together')
        return
    version = check(expected=args.version)
    sha = command('git', 'rev-parse', 'HEAD')
    if args.action == 'preflight':
        remote_state(args.repo, 'v' + version, sha)
    elif args.action == 'finalize':
        finalize(args.assets, version, sha)
    elif args.action == 'verify':
        verify_assets(args.assets, version, sha)
    elif args.action == 'publish':
        publish(args.assets, version, args.repo, args.prerelease)
    print(f'Validated q3mapx {version} at {sha}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f'Release failed: {error}')
