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
                    'apt-mirrors.txt': 'http://azure.archive.ubuntu.com/ubuntu\thttp://archive.ubuntu.com/ubuntu\n',
                    'apt-mirrors-security.txt': 'https://azure.archive.ubuntu.com/ubuntu\nhttp://security.ubuntu.com/ubuntu\n',
                }
                for path, value in entries.items():
                    (apt / path).write_text(value)
                # Run the actual workflow shell, redirecting only its filesystem
                # root and privilege wrapper into the temporary fixture.
                command = block.replace('/etc/apt', str(apt)).replace('sudo sed', 'sed')
                subprocess.run(['bash', '-eu', '-c', command], check=True)
                for path, value in entries.items():
                    expected = re.sub(r'https?://azure.archive.ubuntu.com/ubuntu',
                                      'https://archive.ubuntu.com/ubuntu', value)
                    self.assertEqual((apt / path).read_text(), expected, path)


if __name__ == '__main__':
    unittest.main()
