"""Release, publication and deployment workflows run only in the release mirror.

Globulation2/glob2 carries every workflow, but the release set may do work only
in genixpro/glob2-release, whose master tracks Globulation2/glob2 master
exactly. Each root job (a job with no ``needs``) must check the mirror in its
own ``if:`` so a run anywhere else skips every job without using a runner.

Parses workflow files textually, like test_ci_concurrency (PyYAML is not
available on every runner).
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_ci_concurrency import RELEASE, top_level_block, triggers, workflows  # noqa: E402

MIRROR_TERMS = ("github.repository == 'genixpro/glob2-release'", 'github.repository_id == 1397722696')
# Anything that can publish: deployment environments, named secrets, write tokens.
PUBLISHING = re.compile(r"^    environment:|secrets\.(?!GITHUB_TOKEN\b)[A-Z]|secrets: inherit|"
                        r"(?:contents|packages|id-token|deployments): write", re.M)


def jobs(text):
    """Map each job name to (needs, if-expression or None)."""
    block = top_level_block(text, 'jobs') or ''
    found = {}
    for match in re.finditer(r'^  ([A-Za-z0-9_-]+):\n((?:(?:    .*|\s*)\n)*)', block + '\n', re.M):
        body = match.group(2)
        needs = re.search(r'^    needs:\s*(\S.*)?$', body, re.M)
        condition = re.search(r'^    if:[ \t]*(.*)\n((?:      .*\n)*)', body, re.M)
        if condition:
            head = condition.group(1).strip()
            expression = condition.group(2) if head in ('>-', '>', '|', '|-') else head
            condition = ' '.join(expression.split())
        found[match.group(1)] = (needs is not None, condition)
    return found


def conjuncts(expression):
    """Top-level ``&&`` terms, so a guard inside ``||`` or parentheses doesn't count."""
    expression = expression.strip()
    if expression.startswith('${{') and expression.endswith('}}'):
        expression = expression[3:-2].strip()
    while expression.startswith('(') and closing(expression, 0) == len(expression) - 1:
        expression = expression[1:-1].strip()
    terms, depth, start, quoted = [], 0, 0, False
    for index, char in enumerate(expression):
        if char == "'":
            quoted = not quoted
        elif quoted:
            continue
        elif char == '(':
            depth += 1
        elif char == ')':
            depth -= 1
        elif depth == 0 and expression.startswith('&&', index):
            terms.append(expression[start:index].strip())
            start = index + 2
        elif depth == 0 and expression.startswith('||', index):
            return []
    return terms + [expression[start:].strip()]


def closing(expression, opening):
    depth = 0
    for index in range(opening, len(expression)):
        depth += {'(': 1, ')': -1}.get(expression[index], 0)
        if depth == 0:
            return index
    return -1


def mirror_guarded(condition):
    return condition is not None and all(term in conjuncts(condition) for term in MIRROR_TERMS)


class ReleaseGuardTest(unittest.TestCase):
    def test_every_root_job_requires_the_release_mirror(self):
        for name in sorted(RELEASE):
            roots = {job: condition for job, (needs, condition) in jobs(workflows()[name]).items() if not needs}
            self.assertTrue(roots, name)
            for job, condition in roots.items():
                with self.subTest(workflow=name, job=job):
                    self.assertTrue(mirror_guarded(condition),
                                    f'root job needs a job-level if: requiring {" and ".join(MIRROR_TERMS)}')

    def test_release_workflows_never_run_for_pull_requests(self):
        for name in sorted(RELEASE):
            with self.subTest(workflow=name):
                self.assertFalse({'pull_request', 'pull_request_target'} & triggers(workflows()[name]))

    def test_publishing_workflows_are_in_the_release_set(self):
        for name, text in workflows().items():
            with self.subTest(workflow=name):
                if PUBLISHING.search(text):
                    self.assertIn(name, RELEASE)

    def test_guard_check_rejects_bypasses(self):
        guard = "github.repository == 'genixpro/glob2-release' && github.repository_id == 1397722696"
        self.assertTrue(mirror_guarded(f"${{{{ ({guard} && github.ref == 'refs/heads/master') }}}}"))
        self.assertTrue(mirror_guarded(f"{guard} && (github.event_name == 'schedule' || github.actor_id == 1)"))
        self.assertFalse(mirror_guarded(f"{guard} || github.event_name == 'push'"))
        self.assertFalse(mirror_guarded(f"github.event_name == 'push' || ({guard})"))
        self.assertFalse(mirror_guarded("github.repository_id == 1397722696"))
        self.assertFalse(mirror_guarded(None))
        parsed = jobs("jobs:\n  a:\n    if: >-\n      x &&\n      y\n    runs-on: z\n\n  b:\n    needs: a\n")
        self.assertEqual(parsed, {'a': (False, 'x && y'), 'b': (True, None)})


if __name__ == '__main__':
    unittest.main()
