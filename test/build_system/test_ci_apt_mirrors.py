"""Exercise bootstrap rewrites against direct and mirror-list APT sources."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AptMirrorTest(unittest.TestCase):
    def test_all_bootstrap_blocks_rewrite_mirror_lists_and_direct_sources(self):
        blocks = []
        for name in ('build.yml', 'ci-linux-build.yml', 'thread-sanitizer.yml'):
            text = (ROOT / '.github/workflows' / name).read_text()
            blocks.extend((name, block) for block in re.findall(
                r'          for source in .*?\n          done', text, re.S))
        self.assertEqual(len(blocks), 4)
        for name, block in blocks:
            with self.subTest(workflow=name), tempfile.TemporaryDirectory() as directory:
                apt = Path(directory)
                (apt / 'sources.list.d').mkdir()
                entries = {
                    'sources.list': 'deb http://azure.archive.ubuntu.com/ubuntu noble main\n',
                    'sources.list.d/legacy.list': 'deb https://azure.archive.ubuntu.com/ubuntu jammy main\n',
                    'sources.list.d/ubuntu.sources': 'Types: deb\nURIs: mirror+file:/etc/apt/apt-mirrors.txt\nSuites: noble noble-updates\nComponents: main\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n',
                    'apt-mirrors.txt': 'http://azure.archive.ubuntu.com/ubuntu\thttp://archive.ubuntu.com/ubuntu\n'
                                       'http://us-east-2.ec2.archive.ubuntu.com/ubuntu\tpriority:1\n',
                    'sources.list.d/regional.list': 'deb http://us-east-2.ec2.archive.ubuntu.com/ubuntu noble main\n',
                    'apt-mirrors-security.txt': 'https://azure.archive.ubuntu.com/ubuntu\nhttp://security.ubuntu.com/ubuntu\n',
                }
                for path, value in entries.items():
                    (apt / path).write_text(value)
                # Run the actual workflow shell, redirecting only its filesystem
                # root and privilege wrapper into the temporary fixture.
                command = block.replace('/etc/apt', str(apt)).replace('sudo sed', 'sed')
                subprocess.run(['bash', '-eu', '-c', command], check=True)
                for path, value in entries.items():
                    expected = re.sub(r'https?://(azure|[a-z0-9-]+\.ec2)\.archive\.ubuntu\.com/ubuntu',
                                      'https://archive.ubuntu.com/ubuntu', value)
                    self.assertEqual((apt / path).read_text(), expected, path)


    def entries(self):
        return {
            'sources.list': 'deb http://azure.archive.ubuntu.com/ubuntu noble main\n',
            'sources.list.d/legacy.list': 'deb https://azure.archive.ubuntu.com/ubuntu jammy main\n',
            'sources.list.d/regional.list': 'deb http://us-east-2.ec2.archive.ubuntu.com/ubuntu noble main\n',
            'apt-mirrors.txt': 'http://us-east-2.ec2.archive.ubuntu.com/ubuntu\tpriority:1\n',
            'apt-mirrors-security.txt': 'http://security.ubuntu.com/ubuntu\n',
        }

    def test_shared_script_rewrites_mirrors_and_bounds_stalls(self):
        with tempfile.TemporaryDirectory() as directory:
            apt = Path(directory)
            (apt / 'sources.list.d').mkdir()
            for path, value in self.entries().items():
                (apt / path).write_text(value)
            subprocess.run(['bash', str(ROOT / '.github/scripts/ci_apt_network.sh')], check=True,
                           env={'APT_ROOT': str(apt), 'PATH': '/usr/bin:/bin'})
            for path, value in self.entries().items():
                expected = re.sub(r'https?://(azure|[a-z0-9-]+\.ec2)\.archive\.ubuntu\.com/ubuntu',
                                  'https://archive.ubuntu.com/ubuntu', value)
                self.assertEqual((apt / path).read_text(), expected, path)
            self.assertIn('Acquire::http::Timeout "30";', (apt / 'apt.conf.d/80-ci-network').read_text())

    def test_every_build_apt_update_is_guarded(self):
        # A runner's slow regional mirror stalled unguarded steps past their
        # job timeouts, so every update first rewrites the mirrors.
        lines = (ROOT / '.github/workflows/build.yml').read_text().splitlines()
        for number, line in enumerate(lines):
            if 'apt-get update' not in line:
                continue
            window = '\n'.join(lines[max(0, number - 20):number])
            with self.subTest(line=number + 1):
                self.assertTrue('ci_apt_network.sh' in window or 'archive.ubuntu.com/ubuntu#g' in window,
                                f'build.yml:{number + 1} updates apt without the mirror rewrite')
        # Jobs may set a default working directory (platform runs in
        # services/online), so the script is always reached from the workspace.
        for number, line in enumerate(lines):
            if 'ci_apt_network.sh' in line:
                with self.subTest(call=number + 1):
                    self.assertIn('"$GITHUB_WORKSPACE/.github/scripts/ci_apt_network.sh"', line)


if __name__ == '__main__':
    unittest.main()
