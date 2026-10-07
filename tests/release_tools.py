"""Release contract tests; no network or credentials. SPDX-License-Identifier: GPL-3.0-or-later."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import release
import package_release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.parent = ROOT / '.agents/tmp/release-tools'
        self.parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=self.parent)
        self.root = Path(self.temporary.name).resolve()
        self.write('VERSION', '0.4.0\n')
        self.write('CHANGELOG.md', '# Changelog\n\n## [Unreleased]\n\n'
                   '## [0.4.0] - 2026-10-07\n\n### Added\n\n- First release.\n\n'
                   '[Unreleased]: https://github.com/themuffinator/q3mapx/compare/v0.4.0...HEAD\n'
                   '[0.4.0]: https://github.com/themuffinator/q3mapx/releases/tag/v0.4.0\n')

    def tearDown(self):
        self.assertTrue(self.root.is_relative_to(self.parent.resolve()))
        self.assertFalse(any(p.is_symlink() or getattr(p.lstat(), 'st_file_attributes', 0) & 0x400
                             for p in [self.root, *self.root.rglob('*')]))
        self.temporary.cleanup()

    def write(self, name, value):
        (self.root / name).write_text(value, encoding='utf-8')

    def pending(self):
        path = self.root / 'CHANGELOG.md'
        self.write('CHANGELOG.md', path.read_text().replace('## [Unreleased]',
                   '## [Unreleased]\n\n### Fixed\n\n- Preserve compiled outputs.'))

    def test_current_release_and_notes(self):
        self.assertEqual(release.check(self.root, '0.4.0'), '0.4.0')
        self.assertNotIn('[Unreleased]:', release.sections(self.root)['0.4.0'])
        release.check(ROOT)

    def test_invalid_versions_are_rejected(self):
        for value in ('v0.4.0', '01.2.3', '1.2', '1.2.3-rc.1', '1.2.3\n0.0.0', '../1.2.3', '1.2.3; echo bad'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                release.version_tuple(value)

    def test_version_and_notes_must_agree(self):
        with self.assertRaises(ValueError):
            release.check(self.root, '0.5.0')
        self.write('VERSION', '0.5.0\n')
        with self.assertRaises(ValueError):
            release.check(self.root)

    def test_prepare_moves_notes_and_updates_links(self):
        self.pending()
        with self.assertRaises(ValueError):
            release.check(self.root, '0.4.0')
        release.prepare('0.5.0', self.root, '2026-10-08')
        self.assertEqual(release.check(self.root, '0.5.0'), '0.5.0')
        entries = release.sections(self.root)
        self.assertEqual(entries['Unreleased'], '')
        self.assertIn('Preserve compiled outputs.', entries['0.5.0'])
        self.assertIn('First release.', entries['0.4.0'])
        text = (self.root / 'CHANGELOG.md').read_text()
        self.assertIn('compare/v0.5.0...HEAD', text)
        self.assertIn('compare/v0.4.0...v0.5.0', text)

    def test_prepare_cannot_reuse_or_downgrade_versions(self):
        self.pending()
        before = (self.root / 'CHANGELOG.md').read_bytes()
        for value in ('0.4.0', '0.3.9'):
            with self.assertRaises(ValueError):
                release.prepare(value, self.root)
        self.assertEqual((self.root / 'CHANGELOG.md').read_bytes(), before)

    def test_empty_notes_and_invalid_dates_fail(self):
        with self.assertRaises(ValueError):
            release.prepare('0.4.1', self.root)
        self.pending()
        with self.assertRaises(ValueError):
            release.prepare('0.4.1', self.root, '2026-02-30')
        self.assertEqual(release.read_version(self.root), '0.4.0')

    def test_duplicate_and_unordered_releases_fail(self):
        for extra in ('## [0.4.0] - 2026-10-06\n- Duplicate\n',
                      '## [0.5.0] - 2026-10-06\n- Out of order\n'):
            self.write('CHANGELOG.md', '## [Unreleased]\n\n## [0.4.0] - 2026-10-07\n- Release\n' + extra)
            with self.assertRaises(ValueError):
                release.check(self.root)

    def assets(self):
        assets = self.root / 'assets'
        assets.mkdir()
        for name in release.asset_names('0.4.0'):
            (assets / name).write_bytes(('test asset ' + name).encode())
        release.finalize(assets, '0.4.0', 'a' * 40)
        return assets

    def test_assets_are_complete_and_bound_to_source(self):
        assets = self.assets()
        release.verify_assets(assets, '0.4.0', 'a' * 40)
        with self.assertRaises(ValueError):
            release.verify_assets(assets, '0.4.0', 'b' * 40)
        (assets / 'unwanted.txt').write_text('unexpected')
        with self.assertRaises(ValueError):
            release.verify_assets(assets, '0.4.0', 'a' * 40)

    def test_modified_download_fails_verification(self):
        assets = self.assets()
        (assets / 'q3mapx-0.4.0-source.zip').write_bytes(b'corrupted')
        with self.assertRaises(ValueError):
            release.verify_assets(assets, '0.4.0', 'a' * 40)

    def test_missing_asset_cannot_be_finalized(self):
        assets = self.root / 'assets'
        assets.mkdir()
        with self.assertRaises(ValueError):
            release.finalize(assets, '0.4.0', 'a' * 40)

    def test_windows_staging_preserves_dotted_version_and_exact_sources(self):
        import zipfile
        name = 'q3mapx-0.4.0-windows-x64'
        package = self.root / 'build/package' / name
        package.mkdir(parents=True)
        source_dir = package.parent / 'dependency-sources'
        source_dir.mkdir()
        source = source_dir / 'runtime-1.2.3.src.tar.zst'
        source.write_bytes(b'exact dependency source')
        archive = package.parent / (name + '.zip')
        archive.write_bytes(b'tested portable archive')
        manifest = {'version': '0.4.0', 'revision': 'a' * 40, 'dirty_source_snapshot': False,
                    'dependency_sources_downloaded': True,
                    'packages': [{'source_archive': source.name, 'source_sha256': release.digest(source)}]}
        (package / 'runtime-manifest.json').write_text(json.dumps(manifest))
        (package / 'RUNTIME-CREDITS.md').write_text('Runtime attribution')
        output = self.root / 'assets'
        output.mkdir()
        with patch.object(package_release, 'ROOT', self.root):
            package_release.windows(output, '0.4.0', 'a' * 40)
        self.assertEqual((output / archive.name).read_bytes(), archive.read_bytes())
        with zipfile.ZipFile(output / 'q3mapx-0.4.0-windows-dependency-sources.zip') as bundle:
            self.assertEqual(bundle.read('dependency-sources/' + source.name), source.read_bytes())
            self.assertEqual(set(bundle.namelist()), {'dependency-sources/' + source.name,
                                                     'runtime-manifest.json', 'RUNTIME-CREDITS.md'})

    def test_published_release_is_never_changed(self):
        with patch.object(release, 'api', return_value={'draft': False}) as api:
            with self.assertRaises(ValueError):
                release.remote_state('owner/repo', 'v0.4.0', 'a' * 40)
            api.assert_called_once()

    def test_existing_tag_cannot_move(self):
        with patch.object(release, 'api', side_effect=[None, {'object': {}}, {'sha': 'b' * 40}]):
            with self.assertRaises(ValueError):
                release.remote_state('owner/repo', 'v0.4.0', 'a' * 40)

    def test_same_commit_draft_can_resume(self):
        draft = {'draft': True, 'target_commitish': 'a' * 40}
        with patch.object(release, 'api', side_effect=[draft, {'object': {}}, {'sha': 'a' * 40}]):
            self.assertEqual(release.remote_state('owner/repo', 'v0.4.0', 'a' * 40)[0], draft)

    def test_api_errors_are_not_treated_as_missing_releases(self):
        result = subprocess.CompletedProcess([], 1, '', 'gh: Forbidden (HTTP 403)')
        with patch.object(release.subprocess, 'run', return_value=result), self.assertRaises(RuntimeError):
            release.api('repos/owner/repo/releases/tags/v0.4.0', missing_ok=True)

    def test_upload_verification_gates_publication(self):
        assets = self.assets()
        uploaded = [{'name': p.name, 'size': p.stat().st_size, 'digest': 'sha256:' + release.digest(p)}
                    for p in assets.iterdir()]
        for corrupt in (True, False):
            draft = {'draft': True, 'assets': [dict(item) for item in uploaded]}
            if corrupt:
                draft['assets'][0]['digest'] = 'sha256:' + '0' * 64
            results = [{}, draft, {'draft': False, 'prerelease': False, 'html_url': 'https://example.test/release'}]

            def commands(*args):
                return 'a' * 40 if args == ('git', 'rev-parse', 'HEAD') else ''

            with patch.object(release, 'check'), patch.object(release, 'sections', return_value={'0.4.0': '- Notes'}), \
                    patch.object(release, 'remote_state', return_value=(None, None)), \
                    patch.object(release, 'api', side_effect=results), \
                    patch.object(release, 'command', side_effect=commands) as commands_mock:
                if corrupt:
                    with self.assertRaises(ValueError):
                        release.publish(assets, '0.4.0', 'owner/repo')
                else:
                    release.publish(assets, '0.4.0', 'owner/repo')
                published = any('--draft=false' in call.args for call in commands_mock.call_args_list)
                self.assertEqual(published, not corrupt)


if __name__ == '__main__':
    unittest.main()
