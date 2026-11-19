"""Dependency and tool caches are saved only after the step that sealed them succeeds.

Cache keys are immutable. A save that runs after a failed build stores an unsealed
prefix under the real key; every later run restores it, discards it on
verification, rebuilds from upstream sources and skips the save on the cache hit,
so the cache never recovers.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / '.github/workflows'
STEP = re.compile(r'^(\s*)- (?:name|uses|id):', re.M)


def steps(text):
    starts = [m.start() for m in STEP.finditer(text)]
    return [text[a:b] for a, b in zip(starts, starts[1:] + [len(text)])]


def condition(step):
    match = re.search(r'^\s*if: (.*)$', step, re.M)
    return match.group(1) if match else ''


class DependencyCacheSaveTest(unittest.TestCase):
    def test_sealed_prefixes_are_saved_only_after_a_successful_seal_or_build(self):
        checked = 0
        for workflow in sorted(WORKFLOWS.glob('*.yml')):
            text = workflow.read_text()
            ids = set(re.findall(r'^\s*id: (\S+)$', text, re.M))
            for step in steps(text):
                if 'actions/cache/save@' not in step:
                    continue
                key = re.search(r'key: (\S+)', step)
                if not key or not re.match(r'[a-z0-9-]*-(dependencies|tools)-v\d+-', key.group(1)):
                    continue
                checked += 1
                with self.subTest(workflow=workflow.name, key=key.group(1)):
                    gate = re.findall(r"steps\.([A-Za-z0-9_-]+)\.outcome == 'success'", condition(step))
                    self.assertTrue(gate, 'cache save must require the sealing step to succeed')
                    for step_id in gate:
                        self.assertIn(step_id, ids)
        self.assertGreaterEqual(checked, 3)


if __name__ == '__main__':
    unittest.main()
