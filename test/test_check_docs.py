"""Documentation checker behavior, independent of the repository's current pages."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('check_docs', Path(__file__).resolve().parents[1] / 'tools/check_docs.py')
docs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(docs)


class DocumentationCheckTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.paths = []

    def page(self, name, text):
        path = Path(name)
        (self.root / path).parent.mkdir(parents=True, exist_ok=True)
        (self.root / path).write_text(text)
        self.paths.append(path)

    def check(self, **config):
        return docs.check(self.root, self.paths, config)[0]

    def test_reference_links_images_encoded_paths_and_fences(self):
        self.page('docs/README.md', '# Docs\n[guide][g]\n\n[g]: <a%20guide.md#code-api>\n![image](plot%20one.png)\n\n```md\n[example](missing.md)\n```\n')
        self.page('docs/a guide.md', '# A guide\n## `Code` *API*\n')
        (self.root / 'docs/plot one.png').write_bytes(b'image')
        self.assertEqual(self.check(), [])

    def test_missing_reference_destination_is_reported(self):
        self.page('docs/README.md', '# Docs\n[guide][g]\n\n[g]: nowhere.md\n')
        self.assertIn('missing target', self.check()[0])

    def test_duplicate_heading_suffixes_and_unicode(self):
        anchors, _, _ = docs.parse('# Intro\n## Same\n## Same\n## Same-1\n## Café & tests!\n')
        self.assertEqual(anchors, {'intro', 'same', 'same-1', 'same-1-1', 'café--tests'})

    def test_explicit_html_anchors_and_image_targets(self):
        self.page('docs/README.md', '# Docs\n<a id="start"></a>\n<a name="old"></a>\n[start](#start) [old](#old)\n<img src="missing.png">\n')
        errors = self.check()
        self.assertEqual(len(errors), 1)
        self.assertIn('missing.png', errors[0])

    def test_missing_anchor_and_repository_escape(self):
        self.page('docs/README.md', '# Docs\n[bad](#missing) [escape](../../outside.md)\n')
        errors = self.check()
        self.assertTrue(any('missing anchor' in error for error in errors))
        self.assertTrue(any('escapes repository' in error for error in errors))

    def test_navigation_order_and_unreachable_pages(self):
        self.page('docs/README.md', '# Docs\n[B](b/README.md) [A](a/README.md)\n')
        self.page('docs/a/README.md', '# A\n')
        self.page('docs/b/README.md', '# B\n')
        self.page('docs/orphan.md', '# Orphan\n')
        errors = self.check(categories=['a', 'b'])
        self.assertTrue(any('out of order' in error for error in errors))
        self.assertTrue(any('orphan.md: unreachable' in error for error in errors))

    def test_missing_or_excluded_main_index_cannot_disable_navigation_checks(self):
        self.page('docs/a/README.md', '# A\n')
        self.assertIn('required main index', self.check(categories=['a'])[0])
        self.page('docs/README.md', '# Docs\n[A](a/README.md)\n')
        self.assertIn('required main index', self.check(categories=['a'], excluded={'docs/README.md': 'Bad exception'})[0])

    def test_exemptions_are_narrow_and_links_still_checked(self):
        self.page('docs/README.md', '# Docs\n')
        self.page('docs/generated.md', 'Generated\n[broken](missing.md)\n')
        errors = self.check(structure_exempt={'docs/generated.md': 'Generated'}, reachability_exempt={'docs/generated.md': 'Generated'})
        self.assertEqual(len(errors), 1)
        self.assertIn('missing target', errors[0])

    def test_external_links_are_collected_without_network_requests(self):
        self.page('docs/README.md', '# Docs\n[remote](https://example.com/help#topic) [mail](mailto:test@example.com)\n')
        errors, external, _ = docs.check(self.root, self.paths, {})
        self.assertEqual(errors, [])
        self.assertEqual(external, {'https://example.com/help#topic'})

    def test_ignored_example_is_explicit(self):
        self.page('docs/README.md', '# Docs\n[example](example.md)\n')
        self.assertEqual(self.check(ignored_targets={'example.md': 'Intentional example'}), [])


if __name__ == '__main__':
    unittest.main()
