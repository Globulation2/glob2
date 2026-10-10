"""Publication trust boundaries and final-byte package handoffs."""
import importlib.util
import json
import os
import re
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'mobile'))
sys.path.insert(0,str(ROOT/'tools/release'))
import sideload_release
import sign_macos
import stage_downloads
import linux_installation
from test_release_guards import jobs, conjuncts


class ReleaseWorkflowTests(unittest.TestCase):
    def test_mac_store_build_and_signing_use_same_public_tag_source(self):
        text=(ROOT/'.github/workflows/mac-app-store.yml').read_text()
        build,upload=text.split('\n  upload:\n',1)
        self.assertIn('tag:\n        description: Immutable public release tag',build)
        self.assertIn('ref: ${{ inputs.tag }}',build)
        self.assertIn('repository: Globulation2/glob2',build)
        self.assertIn('python3 tools/release/release.py check --tag "$RELEASE_TAG"',build)
        self.assertIn('refs/tags/$RELEASE_TAG^{commit}',build)
        self.assertIn('source_commit: ${{ steps.source.outputs.commit }}',build)
        self.assertNotIn('environment:',build)
        self.assertNotIn('secrets.',build)
        self.assertIn('ref: ${{ needs.build.outputs.source_commit }}',upload)
        self.assertIn('repository: Globulation2/glob2',upload)
        self.assertIn('source-commit.txt)" = "$EXPECTED_COMMIT"',upload)
        self.assertIn('git rev-parse HEAD > artifacts/mac-app-store/source-commit.txt',build)
        self.assertIn('environment: mac-app-store',upload)

    def test_mac_store_selects_commands_from_compiled_source_and_fails_closed(self):
        workflow=(ROOT/'.github/workflows/mac-app-store.yml').read_text()
        selector=workflow.split('          if test -f src/app/cli/CommandLine.h; then\n',1)[1].split('          # Installed tools',1)[0]
        selector='if test -f src/app/cli/CommandLine.h; then\n'+selector
        script='set -euo pipefail\n'+selector+'printf "%s\\n" "${catalog_args[*]}" "${generate_args[*]}"\n'
        for version,expected in ((None,('--list-map-generators','--generate-map maze')),
                                 (2,('map generators','map generate maze')),
                                 (3,None)):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as directory:
                if version is not None:
                    header=Path(directory)/'src/app/cli/CommandLine.h'
                    header.parent.mkdir(parents=True)
                    header.write_text(f'inline constexpr int Version = {version};\n')
                result=subprocess.run(['bash','-c',script],cwd=directory,text=True,capture_output=True)
                if expected is None:
                    self.assertNotEqual(result.returncode,0)
                    self.assertEqual(result.stdout,'')
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(result.stdout.splitlines(),list(expected))

    def test_gog_builds_and_stages_the_resolved_public_source(self):
        text=(ROOT/'.github/workflows/gog-staging.yml').read_text()
        for platform,next_job in (('windows','windows-smoke'),('linux','macos'),('macos','upload-staging')):
            block=text.split('\n  '+platform+':',1)[1].split('\n  '+next_job+':',1)[0]
            with self.subTest(platform=platform):
                self.assertIn('repository: Globulation2/glob2',block)
                self.assertIn('ref: ${{ needs.preflight.outputs.source_commit }}',block)
                self.assertIn('working-directory: source',block)
                self.assertIn('test "$(git rev-parse HEAD)" = "$EXPECTED_COMMIT"',block)
                self.assertIn('../driver/tools/gog_release.py stage-'+platform+' --source-root "$PWD"',block)
                self.assertIn('path: source/artifacts/gog/'+platform+'.tar.gz',block)
        smoke=text.split('\n  windows-smoke:',1)[1].split('\n  linux:',1)[0]
        self.assertIn('needs: [preflight, windows]',smoke)
        self.assertIn('ref: ${{ needs.preflight.outputs.source_commit }}',smoke)
        self.assertIn("default { throw 'Unsupported source CLI version' }",smoke)
        self.assertIn("'1' { $repeatArgs = @('--nox', $save, '10', '1') }",smoke)
        self.assertIn("'2' { $repeatArgs = @('game', 'repeat', $save, '--ticks', '10', '--runs', '1') }",smoke)
        self.assertIn('& .\\glob2.exe @repeatArgs',smoke)
        self.assertIn('if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }',smoke)

    def test_gog_posix_repeat_selector_preserves_both_command_contracts(self):
        text=(ROOT/'.github/workflows/gog-staging.yml').read_text()
        for platform in ('linux','macos'):
            block=text.split('\n  '+platform+':',1)[1]
            selector='case "$CLI_VERSION" in'+block.split('case "$CLI_VERSION" in',1)[1].split('esac',1)[0]+'esac'
            for version,expected in (('1',['--nox','SAVE','10','1']),
                                     ('2',['game','repeat','SAVE','--ticks','10','--runs','1']),
                                     ('3',None)):
                with self.subTest(platform=platform,version=version), tempfile.TemporaryDirectory() as directory:
                    script='set -euo pipefail\n'+selector+'\nprintf "%s\\n" "${repeat_args[@]}"'
                    result=subprocess.run(['bash','-c',script],cwd=directory,
                                          env={**os.environ,'CLI_VERSION':version},capture_output=True,text=True)
                    if expected is None:
                        self.assertNotEqual(result.returncode,0)
                    else:
                        expected=[str(Path(directory).resolve()/'games/cross-replay.game.gz') if arg=='SAVE' else arg for arg in expected]
                        self.assertEqual(result.returncode,0,result.stderr)
                        self.assertEqual(result.stdout.splitlines(),expected)

    def test_epic_smoke_uses_tag_cli_and_rejects_unknown_versions(self):
        text=(ROOT/'.github/workflows/epic-windows-release.yml').read_text()
        selector='if test -f src/app/cli/CommandLine.h; then'+text.split('if test -f src/app/cli/CommandLine.h; then',1)[1].split('fi',1)[0]+'fi'
        for version,expected in ((None,'cli_version=1'),(2,'cli_version=2'),(3,None)):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as directory:
                if version is not None:
                    header=Path(directory)/'src/app/cli/CommandLine.h'
                    header.parent.mkdir(parents=True)
                    header.write_text(f'inline constexpr int Version = {version};\n')
                output=Path(directory)/'output'
                result=subprocess.run(['bash','-c','set -euo pipefail\n'+selector],cwd=directory,
                                      env={**os.environ,'GITHUB_OUTPUT':str(output)},capture_output=True,text=True)
                if expected is None:
                    self.assertNotEqual(result.returncode,0)
                    self.assertFalse(output.exists())
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(output.read_text().strip(),expected)
        smoke=text.split('\n  smoke-test:',1)[1].split('\n  upload-dev:',1)[0]
        self.assertIn('CLI_VERSION: ${{ needs.preflight.outputs.cli_version }}',smoke)
        self.assertIn("'1' { $repeatArgs = @('--nox', $save, '10', '1') }",smoke)
        self.assertIn("'2' { $repeatArgs = @('game', 'repeat', $save, '--ticks', '10', '--runs', '1') }",smoke)
        self.assertIn("default { throw 'Unsupported source CLI version' }",smoke)
        self.assertIn('if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }',smoke)

    def test_steam_tag_identity_survives_package_smoke_and_upload(self):
        package=(ROOT/'.github/workflows/steam-windows-package.yml').read_text()
        upload=(ROOT/'.github/workflows/steam-windows-upload.yml').read_text()
        self.assertEqual(package.count('description: Immutable public release tag'),1)
        self.assertIn('workflow_call:\n    inputs:\n      tag:\n        required: true',package)
        self.assertIn('value: ${{ jobs.package.outputs.source_sha }}',package)
        build=package.split('\n  package:',1)[1].split('\n  smoke-test:',1)[0]
        smoke=package.split('\n  smoke-test:',1)[1]
        for block in (build,smoke):
            self.assertIn('repository: Globulation2/glob2',block)
            self.assertIn('ref: ${{ needs.preflight.outputs.source_sha }}',block)
            self.assertIn('glob2-steam-windows-${{ needs.preflight.outputs.source_sha }}',block)
        self.assertIn('git rev-parse HEAD > artifacts/steam/windows/source-commit.txt',build)
        self.assertIn('sha256sum source-commit.txt >> SHA256SUMS.txt',build)
        self.assertIn('test "$(git rev-parse HEAD)" = "${{ needs.preflight.outputs.source_sha }}"',build)
        self.assertIn('CLI_VERSION: ${{ needs.preflight.outputs.cli_version }}',smoke)
        self.assertIn("default { throw 'Unsupported source CLI version' }",smoke)
        self.assertIn('if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }',smoke)
        self.assertIn('tag: ${{ inputs.tag }}',upload)
        self.assertIn('glob2-steam-windows-${{ needs.package.outputs.source_sha }}',upload)
        self.assertIn("if ($source -ne '${{ needs.package.outputs.source_sha }}')",upload)
        self.assertLess(upload.index('Depot source identity mismatch'),upload.index('Upload unpublished build'))

    def test_owner_dispatch_master_guard_on_every_new_entrypoint(self):
        for name in ('github-release.yml','promote-downloads.yml','android-play-internal.yml','ios-testflight.yml','ios-production.yml','release.yml'):
            text=(ROOT/'.github/workflows'/name).read_text()
            for job,(needs,condition) in jobs(text).items():
                if not needs:
                    with self.subTest(workflow=name,job=job):
                        terms=conjuncts(condition)
                        for required in ("github.actor_id == 6193625", "github.triggering_actor == 'genixpro'",
                                         "github.event_name == 'workflow_dispatch'", "github.ref == 'refs/heads/master'"):
                            self.assertIn(required,terms)

    def test_build_jobs_have_no_signing_environments_or_publication_credentials(self):
        for name in ('android-play-internal.yml','ios-testflight.yml'):
            text=(ROOT/'.github/workflows'/name).read_text().split('  build:\n',1)[1]
            build=text.split('\n  release:',1)[0].split('\n  upload:',1)[0]
            self.assertNotIn('environment:',build)
            self.assertNotIn('secrets.',build)
            self.assertIn('needs.credentials.outputs.commit',build)

    def test_only_mobile_upload_callers_inherit_release_secrets(self):
        text=(ROOT/'.github/workflows/github-release.yml').read_text()
        for job in ('play-candidate', 'ios-candidate'):
            block=re.split(r'\n  (?=\S)',text.split('  '+job+':\n',1)[1],maxsplit=1)[0]
            self.assertIn('secrets: inherit',block)
        for job in ('packages', 'android-packages'):
            block=re.split(r'\n  (?=\S)',text.split('  '+job+':\n',1)[1],maxsplit=1)[0]
            self.assertNotIn('secrets:',block)

    def test_desktop_queue_is_scoped_to_the_immutable_release_tag(self):
        text=(ROOT/'.github/workflows/release.yml').read_text()
        self.assertIn('group: release-${{ github.ref }}-${{ inputs.tag }}',text)
        self.assertIn('cancel-in-progress: false',text)

    def test_only_qualified_promotion_makes_public_release_visible(self):
        staged=(ROOT/'.github/workflows/github-release.yml').read_text()
        promote=(ROOT/'.github/workflows/promote-downloads.yml').read_text()
        self.assertIn('--publish-draft',staged)
        self.assertNotIn('--draft=false',staged)
        self.assertLess(promote.index('downloads_manifest.py'),promote.index('--draft=false'))
        self.assertIn('QUALIFICATION_SHA256',promote)
        self.assertIn('isDraft',promote)
        self.assertNotIn('--clobber',promote)
        self.assertIn('staged.read_bytes()!=expected.read_bytes()',promote)
        self.assertIn('Existing draft metadata differs',promote)

    def test_windows_signs_exact_game_and_tests_final_installer(self):
        text=(ROOT/'.github/workflows/github-release.yml').read_text()
        self.assertIn('files: ${{ github.workspace }}/artifacts/stage/Globulation2/glob2.exe',text)
        self.assertNotIn('files-folder-filter: glob2.exe',text)
        self.assertIn('Install, upgrade and uninstall the final signed installer',text)
        self.assertLess(text.index('Timestamp and sign the final installer'),text.index('Install, upgrade and uninstall the final signed installer'))

    def test_all_platform_jobs_receive_the_same_source_revision(self):
        text=(ROOT/'.github/workflows/github-release.yml').read_text()
        self.assertEqual(text.count('source_commit: ${{ needs.context.outputs.revision }}'),4)
        self.assertIn('ref: ${{ needs.context.outputs.revision }}',text)
        for name in ('release.yml','android-play-internal.yml','ios-testflight.yml'):
            self.assertIn('EXPECTED_COMMIT',(ROOT/'.github/workflows'/name).read_text())


class PackageInventoryTests(unittest.TestCase):
    def make_files(self, directory):
        version='0.11.0.0'
        names=[f'glob2-{version}-windows-x86_64-setup.exe',f'glob2-{version}-windows-x86_64.zip',
               f'Glob2-{version}-macos-arm64.dmg',f'Glob2-{version}-macos-x86_64.dmg',
               'glob2.flatpak','globulation2_0.11.0.0_amd64.snap',f'glob2-{version}-1.fc43.x86_64.rpm',
               f'glob2-{version}-linux-x86_64-ubuntu22.04.tar.gz',f'glob2-{version}.tar.gz']
        names += [f'glob2-{version}-android-{abi}.apk' for abi in ('arm64-v8a','armeabi-v7a','x86_64')]
        for name in names: (directory/name).write_bytes(name.encode())

    def test_inventory_has_exact_platform_matrix_and_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);self.make_files(path)
            data=stage_downloads.inventory(path,'v0.11.0.0','a'*40)
            self.assertEqual(len(data['packages']),11)
            self.assertEqual(len(data['sources']),1)
            self.assertTrue(all((path/p['filename']).is_file() for p in data['packages']))
            android={p['architecture'] for p in data['packages'] if p['platform']=='android'}
            self.assertEqual(android,{'arm64','armv7','x86_64'})
            self.assertEqual(stage_downloads.inventory(path,'v0.11.0.0','a'*40),data)

    def test_missing_final_installer_fails_before_publication(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);self.make_files(path)
            (path/'glob2-0.11.0.0-windows-x86_64-setup.exe').unlink()
            with self.assertRaisesRegex(ValueError,'Missing final package'):
                stage_downloads.inventory(path,'v0.11.0.0','a'*40)

    def test_ambiguous_rpm_outputs_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);self.make_files(path)
            (path/'glob2-0.11.0.0-2.fc43.x86_64.rpm').write_bytes(b'other')
            with self.assertRaisesRegex(ValueError,'exactly one'):
                stage_downloads.inventory(path,'v0.11.0.0','a'*40)


class LinuxInstallationTests(unittest.TestCase):
    def test_instructions_install_and_remove_the_bundled_library_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage=Path(tmp)
            with patch.object(linux_installation.subprocess,'check_output',return_value='  libSDL3_image.so.0 => /stage/usr/lib/glob2/libSDL3_image.so.0 (0x1)'):
                linux_installation.write(stage)
            instructions=(stage/'INSTALL.txt').read_text()
            self.assertIn('sudo cp -a usr/lib/glob2/. /usr/lib/glob2/',instructions)
            self.assertIn('/usr/lib/glob2; preserve user data.',instructions)
            self.assertIn('libSDL3_image.so.0',instructions)


class SigningTests(unittest.TestCase):
    def test_sideload_rejects_debug_and_upload_key_aliases(self):
        for alias in ('androiddebugkey','glob2-upload'):
            with self.assertRaisesRegex(ValueError,'permanent key'):
                sideload_release.sign(Path('in'),Path('out'),'arm64-v8a',Path('key'),alias,'a'*64,Path('sdk'))

    def test_sideload_requires_pinned_certificate_and_passwords(self):
        with self.assertRaisesRegex(ValueError,'pinned'):
            sideload_release.sign(Path('in'),Path('out'),'arm64-v8a',Path('key'),'glob2-sideload','bad',Path('sdk'))
        with patch.dict(os.environ,{},clear=True),self.assertRaisesRegex(ValueError,'Missing'):
            sideload_release.sign(Path('in'),Path('out'),'arm64-v8a',Path('key'),'glob2-sideload','a'*64,Path('sdk'))

    def test_direct_mac_download_rejects_app_store_certificate(self):
        with self.assertRaisesRegex(ValueError,'Developer ID Application'):
            sign_macos.sign(Path('app'),Path('output'),'arm64','Apple Distribution: Other',Path('keychain'),Path('key'),'id','issuer')

    def test_mac_architecture_and_minimum_os_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            app=Path(tmp)/'App.app';app.mkdir();(app/'game').write_bytes(b'\xcf\xfa\xed\xfe')
            with patch.object(sign_macos.subprocess,'check_output',return_value='x86_64'),self.assertRaisesRegex(ValueError,'architecture'):
                sign_macos.validate(app,'arm64')
            with patch.object(sign_macos.subprocess,'check_output',side_effect=['arm64','Load command 1\n cmd LC_BUILD_VERSION\n minos 16.0']),self.assertRaisesRegex(ValueError,'macOS 15.0'):
                sign_macos.validate(app,'arm64')


if __name__=='__main__': unittest.main()
