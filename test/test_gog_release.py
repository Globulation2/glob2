"""Checks for GOG depot integrity and the public project contract."""

import argparse
import hashlib
import os
import shutil
import zipfile
import json
import io
import stat
import sys
import tarfile
import tempfile
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from gog_pipeline import render, run_builder
from gog_release import (REQUIRED_ASSETS, extract_archive, preflight, stage_linux,
                         verify_manifest, write_manifest, write_source_offer, RELEASES, inventory,
                         assert_preserved, unpack_release, stage_released, record_host_library, dependencies)


class GOGReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.depot = self.root / "depot"
        self.depot.mkdir()
        for name in REQUIRED_ASSETS:
            path = self.depot / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"asset")
        (self.depot / "glob2.exe").write_bytes(b"binary")
        self.metadata = {"platform": "windows", "version": "0.9.5.0",
                         "source_commit": "a" * 40, "workflow_commit": "b" * 40}
        write_source_offer(self.depot, self.metadata["source_commit"])
        write_manifest(self.depot, self.metadata)

    def test_changed_or_unlisted_files_are_rejected(self):
        self.assertEqual(verify_manifest(self.depot, "windows"), self.metadata)
        (self.depot / "data/fonts/sans.ttf").write_bytes(b"modified")
        with self.assertRaisesRegex(ValueError, "modified"):
            verify_manifest(self.depot, "windows")
        (self.depot / "data/fonts/sans.ttf").write_bytes(b"asset")
        (self.depot / "unlisted").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "inventory"):
            verify_manifest(self.depot, "windows")

    def test_mirror_localized_apple_resource_cannot_claim_public_tag_source(self):
        source = self.root / 'source-guard'
        source.mkdir()

        def git(*args):
            return subprocess.check_output(
                ['git', '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=' + str(source / 'no-hooks'),
                 '-c', 'user.name=Release test', '-c', 'user.email=tests@example.invalid', *args],
                cwd=source, text=True).strip()

        git('init', '-q')
        (source / 'scons').mkdir()
        (source / 'scons/build_layout.py').write_text('PACKAGE_VERSION = "0.11.0.0"\n')
        resource = source / 'platforms/macos/mr.lproj/InfoPlist.strings'
        resource.parent.mkdir(parents=True)
        resource.write_text('"NSLocalNetworkUsageDescription" = "मूळ मजकूर";\n', encoding='utf-8')
        git('add', '.')
        git('commit', '-qm', 'Public release source')
        public = git('rev-parse', 'HEAD')
        with patch('gog_release.ROOT', source):
            self.assertEqual(preflight('v0.11.0.0', public)['source_commit'], public)
            resource.write_text('"NSLocalNetworkUsageDescription" = "बदललेला मजकूर";\n', encoding='utf-8')
            git('add', '.')
            git('commit', '-qm', 'Mirror changes localized permission copy')
            with self.assertRaisesRegex(ValueError, r'game source differs from public tag:.*\n.*platforms/macos/mr.lproj/InfoPlist.strings'):
                preflight('v0.11.0.0', public)

    def test_retired_tutorial_assets_are_rejected_even_when_inventoried(self):
        for name in ('campaigns/Tutorial_Campaign.txt', 'campaigns/tutorial-part1.map.gz',
                     'scripts/tutorial_part1.sgsl'):
            with self.subTest(asset=name):
                retired = self.depot / name
                retired.parent.mkdir(parents=True, exist_ok=True)
                retired.write_text('retired tutorial content')
                write_manifest(self.depot, self.metadata)
                with self.assertRaisesRegex(ValueError, 'retired tutorial'):
                    verify_manifest(self.depot, 'windows')
                retired.unlink()

    def test_every_catalog_lesson_is_required_not_just_introduction(self):
        (self.depot / 'campaigns/defensive-tactics.map.gz').unlink()
        write_manifest(self.depot, self.metadata)
        with self.assertRaisesRegex(ValueError, 'missing asset: campaigns/defensive-tactics.map.gz'):
            verify_manifest(self.depot, 'windows')

    def test_verified_webp_artwork_can_replace_png(self):
        png = self.depot / "data/gfx/ressource0.png"
        png.with_suffix(".webp").write_bytes(b"exported artwork")
        png.unlink()
        write_manifest(self.depot, self.metadata)
        self.assertEqual(verify_manifest(self.depot, "windows"), self.metadata)
        png.with_suffix(".webp").unlink()
        write_manifest(self.depot, self.metadata)
        with self.assertRaisesRegex(ValueError, "missing asset"):
            verify_manifest(self.depot, "windows")

    def test_linux_bundles_sdl3_from_original_dependency_paths(self):
        for sdl_name in ("libSDL3.so.0", "libSDL2-2.0.so.0"):
            with self.subTest(runtime=sdl_name):
                source = (self.root / sdl_name).resolve()
                source.mkdir()
                (source / "docs/assets").mkdir(parents=True)
                (source / "COPYING").write_text("game license")
                (source / "docs/assets/source-attribution.md").write_text("attribution")
                install = source / "install"
                binary = install / "usr/bin/glob2"
                binary.parent.mkdir(parents=True)
                binary.write_bytes(b"game")
                for name in REQUIRED_ASSETS:
                    asset = install / "usr/share/glob2" / name
                    asset.parent.mkdir(parents=True, exist_ok=True)
                    asset.write_bytes(b"asset")
                private = install / "usr/lib/glob2"
                private.mkdir(parents=True)
                sdl = private / sdl_name
                codec = private / "libopus.so.0"
                sdl.write_bytes(b"sdl")
                codec.write_bytes(b"opus")
                dependency_paths = {
                    binary: [(sdl_name, sdl), ("libc.so.6", source / "system-libc")],
                    sdl: [(codec.name, codec)], codec: [],
                }
                args = argparse.Namespace(stage=install, output=source / "depot",
                                          version="0.11.0.0", source_commit="a" * 40,
                                          workflow_commit="b" * 40)
                with patch("gog_release.ROOT", source), patch(
                        "gog_release.dependencies", side_effect=lambda path, library_path: dependency_paths[path]):
                    if sdl_name.startswith("libSDL2"):
                        with self.assertRaisesRegex(ValueError, "SDL3 runtime"):
                            stage_linux(args)
                    else:
                        stage_linux(args)
                        self.assertEqual(verify_manifest(args.output, "linux")["version"], "0.11.0.0")
                        self.assertEqual((args.output / "lib" / codec.name).read_bytes(), b"opus")
                        self.assertFalse((args.output / "lib/libc.so.6").exists())

    def test_project_has_one_primary_task_and_no_credentials(self):
        project = render("windows", self.depot, self.root / "project.json",
                         "0.9.5.0-r123-a1", "1234", "1234")
        data = project["project"]
        self.assertEqual(data["platform"], "windows")
        self.assertEqual(data["version"], "0.9.5.0-r123-a1")
        self.assertEqual(data["products"][0]["tasks"][0]["path"], "glob2.exe")
        self.assertEqual(data["clientSecret"], "")
        self.assertEqual(json.loads((self.root / "project.json").read_text()), project)

    def test_upload_only_targets_staging_and_requires_credentials(self):
        builder = self.root / "builder"
        builder.write_bytes(b"tool")
        with self.assertRaisesRegex(ValueError, "only the private Staging"):
            run_builder(builder, self.root / "project.json", "Master", False)

    def test_linux_launch_permission_is_required(self):
        linux = self.root / "linux"
        linux.mkdir()
        for name in REQUIRED_ASSETS:
            path = linux / "share/glob2" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"asset")
        (linux / "start.sh").write_bytes(b"#!/bin/sh\n")
        write_source_offer(linux, self.metadata["source_commit"])
        write_manifest(linux, {**self.metadata, "platform": "linux"})
        with self.assertRaisesRegex(ValueError, "not executable"):
            verify_manifest(linux, "linux")
        (linux / "start.sh").chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
        self.assertEqual(verify_manifest(linux, "linux")["platform"], "linux")

    def test_archive_cannot_escape_depot_before_credentials_are_used(self):
        archive = self.root / "evil.tar.gz"
        payload = b"bad"
        with tarfile.open(archive, "w:gz") as packed:
            member = tarfile.TarInfo("../outside")
            member.size = len(payload)
            packed.addfile(member, io.BytesIO(payload))
        with self.assertRaises(tarfile.TarError):
            extract_archive(archive, self.root / "extracted", "windows")
        self.assertFalse((self.root / "outside").exists())

    def test_separate_source_checkout_requires_tag_commit_clean_tree_and_version(self):
        source=self.root/'source'
        (source/'scons').mkdir(parents=True)
        layout=source/'scons/build_layout.py'
        layout.write_text('PACKAGE_VERSION = "0.11.0.0"\n')
        tag='v0.11.0.0'
        for selected,dirty,package,error in (
                ('b'*40,'','0.11.0.0',None),
                ('c'*40,'','0.11.0.0','does not match public tag'),
                ('b'*40,' M src/Game.cpp','0.11.0.0','tracked changes'),
                ('b'*40,'','0.10.0.0','PACKAGE_VERSION')):
            layout.write_text(f'PACKAGE_VERSION = "{package}"\n')
            with self.subTest(error=error), \
                 patch('gog_release.command',side_effect=['a'*40,'b'*40]), \
                 patch('gog_release.version',return_value='0.11.0.0'), \
                 patch('gog_release.subprocess.run'), \
                 patch('gog_release.subprocess.check_output',side_effect=[selected,dirty]):
                if error:
                    with self.assertRaisesRegex(ValueError,error):
                        preflight(tag,'refs/gog-upstream/'+tag,source)
                else:
                    result=preflight(tag,'refs/gog-upstream/'+tag,source)
                    self.assertEqual(result['source_commit'],'b'*40)
                    self.assertEqual(result['workflow_commit'],'a'*40)

    def test_public_tag_source_diff_blocks_release(self):
        tag = "v0.9.5.0"
        with patch("gog_release.command", side_effect=["a" * 40, "b" * 40, "SConstruct"]), \
             patch("gog_release.subprocess.run"), patch("gog_release.version", return_value=tag[1:]):
            with self.assertRaisesRegex(ValueError, "differs from public tag"):
                preflight(tag, f"refs/gog-upstream/{tag}")


class ReleasedPayloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.tag = 'v0.11.4.0'
        self.commit = RELEASES[self.tag]['source_commit']

    def assets(self, root):
        for name in REQUIRED_ASSETS:
            p = root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'released asset')
        (root / 'COPYING').write_text('released licence')

    def args(self, archive, platform='windows', architecture='x86_64'):
        return argparse.Namespace(tag=self.tag, source_commit=self.commit, version='0.11.4.0',
            workflow_commit='b'*40, output=self.root/'depot', archive=archive,
            platform=platform, architecture=architecture, source_root=self.root/'source')

    def pins(self, archive, platform='windows', architecture='x86_64'):
        data = dict(RELEASES[self.tag])
        data[platform+'-'+architecture] = (archive.name, hashlib.sha256(archive.read_bytes()).hexdigest())
        return {self.tag: data}

    def test_reuse_cannot_enter_upload_or_unreviewed_tag(self):
        for tag, mode in [(self.tag, 'stage'), ('v99.0', 'build')]:
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, 'build-only'):
                preflight(tag, 'unused', reuse_released=True, mode=mode)

    def test_reuse_preflight_rejects_moved_tag(self):
        with patch('gog_release.version', return_value='0.11.4.0'), \
             patch('gog_release.command', side_effect=['b'*40, 'c'*40]), \
             self.assertRaisesRegex(ValueError, 'reviewed released source'):
            preflight(self.tag, 'tag', reuse_released=True)

    def test_digest_checked_before_extraction(self):
        archive=self.root/'payload.zip'; archive.write_bytes(b'not even an archive')
        with self.assertRaisesRegex(ValueError, 'SHA256'):
            unpack_release(archive,self.root/'unpacked','0'*64,windows=True)
        self.assertFalse((self.root/'unpacked').exists())

    def test_zip_rejects_traversal_symlinks_and_case_collisions(self):
        for entries in [('Globulation2/../../escape',), ('Globulation2/a','Globulation2/A'), ('Globulation2/link',)]:
            archive=self.root/'payload.zip'
            with zipfile.ZipFile(archive,'w') as z:
                for name in entries:
                    entry=zipfile.ZipInfo(name)
                    if name.endswith('/link'):entry.external_attr=(stat.S_IFLNK|0o777)<<16
                    z.writestr(entry,b'payload')
            output=self.root/'unpacked'
            if output.exists():shutil.rmtree(output)
            with self.subTest(entries=entries), self.assertRaisesRegex(ValueError,'unsafe'):
                unpack_release(archive,output,hashlib.sha256(archive.read_bytes()).hexdigest(),windows=True)
        self.assertFalse((self.root/'escape').exists())

    def test_tar_allows_internal_library_links_and_rejects_escape(self):
        for target, valid in [('libreal.so',True),('../../../escape',False)]:
            archive=self.root/'payload.tar.gz'
            with tarfile.open(archive,'w:gz') as tar:
                regular=tarfile.TarInfo('usr/lib/libreal.so');regular.size=3
                tar.addfile(regular,io.BytesIO(b'elf'))
                link=tarfile.TarInfo('usr/lib/libalias.so');link.type=tarfile.SYMTYPE;link.linkname=target
                tar.addfile(link)
            output=self.root/('valid' if valid else 'invalid')
            if valid:
                unpack_release(archive,output,hashlib.sha256(archive.read_bytes()).hexdigest())
                self.assertTrue((output/'usr/lib/libalias.so').is_symlink())
            else:
                with self.assertRaises(tarfile.TarError):
                    unpack_release(archive,output,hashlib.sha256(archive.read_bytes()).hexdigest())

    def test_windows_preserves_released_files_and_checks_inventory_after_rehash(self):
        payload=self.root/'Globulation2';self.assets(payload)
        (payload/'glob2.exe').write_bytes(b'released executable')
        archive=self.root/'payload.zip'
        with zipfile.ZipFile(archive,'w') as z:
            for p in payload.rglob('*'):
                if p.is_file():z.write(p,p.relative_to(self.root))
        args=self.args(archive)
        with patch.dict('gog_release.RELEASES',self.pins(archive),clear=True):
            stage_released(args)
            for p in payload.rglob('*'):
                if p.is_file():self.assertEqual(p.read_bytes(),(args.output/p.relative_to(payload)).read_bytes())
            info=verify_manifest(args.output,'windows')
            self.assertTrue(info['released_payload'])
            # Even a regenerated outer manifest must not hide a changed original payload.
            (args.output/'glob2.exe').write_bytes(b'changed executable')
            write_manifest(args.output,info)
            with self.assertRaisesRegex(ValueError,'payload inventory changed'):
                verify_manifest(args.output,'windows')

    def test_pinned_version_must_not_be_relabelled(self):
        args=self.args(self.root/'absent.zip');args.version='99.0'
        with self.assertRaisesRegex(ValueError,'reviewed binary pins'):
            stage_released(args)
        self.assertFalse(args.output.exists())

    def test_preservation_includes_symlinks_and_modes(self):
        original=self.root/'original';original.mkdir();(original/'binary').write_bytes(b'elf');(original/'binary').chmod(0o755)
        (original/'alias').symlink_to('binary');copied=self.root/'copy';shutil.copytree(original,copied,symlinks=True)
        assert_preserved(original,copied)
        (copied/'binary').chmod(0o644)
        with self.assertRaisesRegex(ValueError,'modes changed'):assert_preserved(original,copied)

    def test_released_mac_is_not_modified_or_resigned_and_source_stays_external(self):
        archive=self.root/'payload.dmg';archive.write_bytes(b'signed dmg fixture')
        args=self.args(archive,'macos','arm64');commands=[]
        def run(command,**kwargs):
            commands.append(command)
            if command[0]=='hdiutil' and command[1]=='attach':
                self.assertIn('-readonly',command)
                app=Path(command[command.index('-mountpoint')+1])/'Glob2.app'
                self.assets(app/'Contents/Resources')
                binary=app/'Contents/MacOS/glob2';binary.parent.mkdir(parents=True);binary.write_bytes(b'signed app fixture');binary.chmod(0o755)
                (app/'Contents/MacOS/alias').symlink_to('glob2')
            elif command[0]=='ditto':shutil.copytree(command[1],command[2],symlinks=True)
            return argparse.Namespace(returncode=0)
        with patch.dict('gog_release.RELEASES',self.pins(archive,'macos','arm64'),clear=True), \
             patch('gog_release.subprocess.run',side_effect=run), \
             patch('gog_release.subprocess.check_output',return_value='arm64'):
            stage_released(args)
            info=verify_manifest(args.output,'macos')
            self.assertEqual(info['architecture'],'arm64')
            self.assertTrue((args.output/'SOURCE.txt').is_file())
            self.assertFalse((args.output/'Glob2.app/Contents/Resources/SOURCE.txt').exists())
            self.assertTrue((args.output/'Glob2.app/Contents/MacOS/alias').is_symlink())
        self.assertFalse(any('--sign' in c or '--force' in c for c in commands))
        self.assertTrue(any(c[:3]==['xcrun','stapler','validate'] for c in commands))

    def test_linux_preserves_released_elf_assets_and_immutable_source_notices(self):
        source=self.root/'source';(source/'docs/assets').mkdir(parents=True)
        (source/'COPYING').write_text('immutable source licence')
        (source/'docs/assets/source-attribution.md').write_text('immutable source attribution')
        payload=self.root/'install';binary=payload/'usr/bin/glob2';binary.parent.mkdir(parents=True);binary.write_bytes(b'released ELF');binary.chmod(0o755)
        self.assets(payload/'usr/share/glob2')
        private=payload/'usr/lib/glob2';private.mkdir(parents=True);sdl=private/'libSDL3.so.0';sdl.write_bytes(b'released private SDL')
        archive=self.root/'payload.tar.gz'
        with tarfile.open(archive,'w:gz') as tar:tar.add(payload/'usr',arcname='usr')
        args=self.args(archive,'linux')
        def linked(path,library_path):
            return [('libSDL3.so.0',library_path/'libSDL3.so.0')] if path.name=='glob2' else []
        with patch.dict('gog_release.RELEASES',self.pins(archive,'linux'),clear=True), \
             patch('gog_release.dependencies',side_effect=linked), \
             patch('gog_release.ubuntu_runtime_host',return_value={'os_release':'Ubuntu22.04fixture'}), \
             patch('gog_release.ROOT',self.root/'wrong-driver-source'):
            stage_released(args)
            self.assertEqual((args.output/'bin/glob2').read_bytes(),binary.read_bytes())
            self.assertEqual((args.output/'lib/libSDL3.so.0').read_bytes(),sdl.read_bytes())
            self.assertEqual((args.output/'COPYING').read_text(),'immutable source licence')
            self.assertEqual((args.output/'source-attribution.md').read_text(),'immutable source attribution')
            info=verify_manifest(args.output,'linux')
            self.assertFalse(info['runtime_licence_review_required'])

    def test_unresolved_linked_library_fails_closed(self):
        with patch('gog_release.subprocess.run',return_value=argparse.Namespace(stdout='libmissing.so => not found')), \
             self.assertRaisesRegex(ValueError,'unresolved library'):
            dependencies(self.root/'released-elf')

    def test_missing_actual_runtime_copyright_blocks_candidate(self):
        library=self.root/'libcodec.so.1';library.write_bytes(b'actual runtime')
        with patch('gog_release.UBUNTU_COPYRIGHT_ROOT',self.root/'notices'), \
             patch('gog_release.subprocess.check_output',side_effect=['codec:amd64: file','codec\t1.2\tcodec-source\t1.2']), \
             self.assertRaisesRegex(ValueError,'missing actual copyright'):
            record_host_library(library,self.root/'depot',{})

    def test_runtime_provenance_copies_actual_notice_and_referenced_licence(self):
        library=self.root/'libcodec.so.1';library.write_bytes(b'actual runtime')
        copyright=self.root/'notices/codec/copyright';copyright.parent.mkdir(parents=True)
        copyright.write_text('See /usr/share/common-licenses/LGPL-2.1 for terms')
        common=self.root/'common/LGPL-2.1';common.parent.mkdir();common.write_text('actual referenced licence fixture')
        records={}
        with patch('gog_release.UBUNTU_COPYRIGHT_ROOT',self.root/'notices'), \
             patch('gog_release.COMMON_LICENSE_ROOT',self.root/'common'), \
             patch('gog_release.subprocess.check_output',side_effect=['codec:amd64: file','codec\t1.2\tcodec-source\t1.2']):
            record_host_library(library,self.root/'depot',records)
        self.assertEqual(records[library.name]['version'],'1.2')
        self.assertEqual(records[library.name]['source_package'],'codec-source')
        self.assertEqual((self.root/'depot/licenses/ubuntu-runtime/codec/copyright').read_bytes(),copyright.read_bytes())
        self.assertEqual((self.root/'depot/licenses/ubuntu-runtime/common-licenses/LGPL-2.1').read_bytes(),common.read_bytes())

    def test_common_licence_sentence_period_preserves_notice_and_real_missing_fails(self):
        library=self.root/'libcodec.so.1';library.write_bytes(b'runtime')
        notice=self.root/'notices/codec/copyright';notice.parent.mkdir(parents=True)
        common=self.root/'common';common.mkdir();(common/'GPL').write_text('actual GPL fixture')
        for reference, succeeds in [('GPL.',True),('REAL-MISSING.',False)]:
            original=('Terms in /usr/share/common-licenses/'+reference+'\n').encode()
            notice.write_bytes(original);records={}
            with self.subTest(reference=reference), \
                 patch('gog_release.UBUNTU_COPYRIGHT_ROOT',self.root/'notices'), \
                 patch('gog_release.COMMON_LICENSE_ROOT',common), \
                 patch('gog_release.subprocess.check_output',side_effect=['codec:amd64: file','codec\t1.2\tcodec-source\t1.2']):
                if succeeds:
                    record_host_library(library,self.root/'depot',records)
                    self.assertEqual((self.root/'depot/licenses/ubuntu-runtime/codec/copyright').read_bytes(),original)
                    self.assertEqual(records[library.name]['referenced_common_licences'][0]['name'],'GPL')
                    self.assertEqual((self.root/'depot/licenses/ubuntu-runtime/common-licenses/GPL').read_bytes(),(common/'GPL').read_bytes())
                else:
                    with self.assertRaisesRegex(ValueError,'common licence is missing: REAL-MISSING'):
                        record_host_library(library,self.root/'depot',records)
            self.assertEqual(notice.read_bytes(),original)

    def test_workflow_released_jobs_have_no_build_or_upload_path(self):
        workflow=(Path(__file__).resolve().parents[1]/'.github/workflows/gog-staging.yml').read_text()
        released=workflow[workflow.index('  released-windows:'):]
        self.assertNotIn('scons ',released)
        self.assertNotIn('GOG_PASSWORD',released)
        self.assertIn('!inputs.reuse_released_payload &&',workflow)
        self.assertIn('"$REUSE_RELEASED" == true && "$RELEASE_MODE" != build',workflow)
        self.assertIn('gog-released-macos-${{ matrix.architecture }}',released)
        self.assertIn('runner: macos-15-intel',released)


if __name__ == "__main__":
    unittest.main()
