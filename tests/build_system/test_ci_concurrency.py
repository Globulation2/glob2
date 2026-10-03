"""Superseded CI runs are cancelled; release, publication and deployment runs are not.

Parses workflow files textually (PyYAML is not available on every runner), so
it only relies on the top-level ``on:`` and ``concurrency:`` blocks.
"""
import importlib.util
from pathlib import Path
import re
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / '.github/workflows'
spec = importlib.util.spec_from_file_location('cancel', ROOT / '.github/scripts/ci_cancel_superseded.py')
cancel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cancel)

STANDARD_CANCEL = ("${{ github.event_name == 'pull_request' || (github.event_name == 'push' && "
                   "startsWith(github.ref, 'refs/heads/') && "
                   "github.ref != format('refs/heads/{0}', github.event.repository.default_branch)) }}")

# Release, publication and deployment workflows: never cancelled in progress.
RELEASE = {
    'amazon-appstore.yml', 'android-play-internal.yml', 'app-signing-fingerprints.yml', 'browser-release.yml',
    'epic-windows-release.yml', 'fdroid-publication.yml', 'fdroid-release-validation.yml',
    'flathub-update.yml', 'github-release.yml', 'ios-testflight.yml', 'publish-desktop.yml',
    'release.yml', 'server-image.yml', 'snap-release.yml', 'steam-windows-upload.yml',
    'windows-store-release.yml', 'steam-windows-package.yml', 'mac-app-store.yml',
}
# Not superseded by newer runs, each with its own reason.
EXEMPT = {
    'thread-sanitizer.yml': 'callable validation inherits build cancellation',
    'ci-metrics.yml': 'each run measures a distinct, completed build run',
    'cancel-superseded.yml': 'the canceller; cancels its own older runs unconditionally',
}


def top_level_block(text, key):
    lines = text.split('\n')
    for index, line in enumerate(lines):
        if re.match(rf'^{re.escape(key)}:', line):
            block = [line]
            for following in lines[index + 1:]:
                if following and not following.startswith((' ', '#')):
                    break
                block.append(following)
            return '\n'.join(block)
    return None


def triggers(text):
    block = top_level_block(text, 'on') or ''
    return set(re.findall(r'^  ([a-z_]+):', block, re.M)) | set(re.findall(r'^on: \[?([a-z_, ]+)\]?', block, re.M))


def concurrency(text):
    block = top_level_block(text, 'concurrency')
    if block is None:
        return None
    group = re.search(r'^  group: (.+)$', block, re.M)
    cancel_line = re.search(r'^  cancel-in-progress: (.+)$', block, re.M)
    return (group.group(1).strip() if group else None,
            cancel_line.group(1).strip() if cancel_line else 'false')


def workflows():
    return {path.name: path.read_text() for path in sorted(WORKFLOWS.glob('*.y*ml'))}


class WorkflowConcurrencyTest(unittest.TestCase):
    def test_classification_names_existing_workflows(self):
        self.assertFalse((RELEASE | set(EXEMPT)) - set(workflows()))

    def test_ci_workflows_cancel_superseded_pull_request_and_branch_runs(self):
        for name, text in workflows().items():
            on = triggers(text)
            if name in RELEASE or name in EXEMPT or on == {'workflow_call'}:
                continue
            with self.subTest(workflow=name):
                declared = concurrency(text)
                self.assertIsNotNone(declared, 'CI workflows need a concurrency group (see reference.md)')
                group, cancel_expression = declared
                self.assertIn('github.ref', group)
                self.assertEqual(cancel_expression, STANDARD_CANCEL)

    def test_called_workflows_never_share_their_callers_group(self):
        # github.workflow names the caller inside a called workflow. A group
        # identical to the caller's deadlocks or cancels the caller.
        for name, text in workflows().items():
            if 'workflow_call' not in triggers(text):
                continue
            declared = concurrency(text)
            with self.subTest(workflow=name):
                if triggers(text) == {'workflow_call'}:
                    self.assertIsNone(declared, 'call-only workflows inherit their caller\'s cancellation')
                elif declared is not None:
                    self.assertFalse(declared[0].startswith('${{'), 'group needs a literal workflow prefix')

    def test_release_workflows_never_cancel_in_progress(self):
        for name in RELEASE:
            declared = concurrency(workflows()[name])
            with self.subTest(workflow=name):
                if declared is not None:
                    self.assertEqual(declared[1], 'false')

    def test_canceller_covers_every_event_triggered_ci_workflow(self):
        texts = workflows()
        # Legacy release-path PR runs remain cancellable, but manual/tag releases never are.
        self.assertNotIn('.github/workflows/release.yml', cancel.CI_WORKFLOWS)
        for path in cancel.CI_WORKFLOWS:
            self.assertIn(path.rsplit('/', 1)[1], texts)
        for name, text in texts.items():
            if name in RELEASE or name in EXEMPT:
                continue
            if triggers(text) & {'pull_request', 'push'}:
                self.assertIn(f'.github/workflows/{name}', cancel.CI_WORKFLOWS, name)
        canceller = texts['cancel-superseded.yml']
        self.assertEqual(concurrency(canceller)[1], 'true')
        self.assertIn('actions: write', canceller)


def run(run_id, sha, branch='feature', event='pull_request', path='.github/workflows/build.yml',
        status='queued', repo='o/r'):
    return dict(id=run_id, head_sha=sha, head_branch=branch, event=event, path=path, status=status,
                head_repository=dict(full_name=repo))


class SelectionTest(unittest.TestCase):
    def select(self, runs, heads, **kwargs):
        return [r['id'] for r in cancel.select_superseded(runs, heads, current_run_id=99, repository='o/r', **kwargs)]

    def test_only_runs_for_an_outdated_head_are_cancelled(self):
        runs = [run(1, 'old'), run(2, 'new'), run(3, 'old', status='completed'),
                run(4, 'old', path='.github/workflows/mac-app-store.yml', status='in_progress')]
        self.assertEqual(self.select(runs, {('pr', 'o/r', 'feature'): 'new'}), [1, 4])

    def test_closed_pull_requests_cancel_everything_but_keep_branch_pushes(self):
        runs = [run(1, 'a'), run(2, 'a', event='push')]
        heads = {('pr', 'o/r', 'feature'): None, ('branch', 'o/r', 'feature'): 'a'}
        self.assertEqual(self.select(runs, heads), [1])

    def test_release_dispatch_current_and_unknown_runs_are_protected(self):
        heads = {('pr', 'o/r', 'feature'): 'new', ('branch', 'o/r', 'feature'): 'new'}
        runs = [run(1, 'old', path='.github/workflows/release.yml'),
                run(2, 'old', path='.github/workflows/publish-desktop.yml', event='workflow_dispatch'),
                run(3, 'old', event='workflow_dispatch'),
                run(99, 'old'),
                run(4, 'old', branch='other'),
                run(5, 'old', path='.github/workflows/build.yml@refs/heads/feature')]
        self.assertEqual(self.select(runs, heads), [5])

    def test_legacy_release_pr_runs_are_retired_even_at_the_current_head(self):
        rows=[run(1,'current',path='.github/workflows/steam-windows-package.yml',status='in_progress'),
              run(2,'current',path='.github/workflows/mac-app-store.yml'),
              run(3,'current',path='.github/workflows/steam-windows-package.yml',event='workflow_dispatch'),
              run(4,'current',path='.github/workflows/mac-app-store.yml',event='workflow_dispatch')]
        self.assertEqual(self.select(rows,{('pr','o/r','feature'):'current'}),[1,2])

    def test_default_branch_is_opt_in_and_keeps_its_newest_run(self):
        runs = [run(1, 'a', branch='master', event='push'), run(2, 'b', branch='master', event='push'),
                run(3, 'c', branch='master', event='schedule')]
        heads = {('branch', 'o/r', 'master'): 'd'}
        self.assertEqual(self.select(runs, heads), [])
        self.assertEqual(self.select(runs, heads, include_default_branch=True), [1])

    def test_fork_branch_with_same_name_is_a_different_head(self):
        runs = [run(1, 'x', repo='fork/r')]
        self.assertEqual(self.select(runs, {('pr', 'o/r', 'feature'): 'new'}), [])


if __name__ == '__main__':
    unittest.main()


class MasterExecutionTest(unittest.TestCase):
    def test_queued_workflow_with_active_or_completed_job_is_protected(self):
        row=run(1,'old',branch='master',event='push')
        for job in [dict(status='in_progress'),dict(status='queued',runner_id=123),
                    dict(status='completed',steps=[dict(started_at='2026-10-03T04:00:00Z')])]:
            self.assertTrue(cancel.master_execution_started('o/r',row,read=lambda *args:[job]))
        self.assertFalse(cancel.master_execution_started('o/r',row,read=lambda *args:[dict(status='queued',runner_id=0),dict(status='completed',conclusion='skipped',steps=[])]))
        with patch.object(cancel,'master_execution_started',return_value=True):
            cancel.protect_started_master('o/r',[row],'master')
        newer=run(2,'new',branch='master',event='push')
        self.assertEqual(cancel.select_superseded([row,newer],{('branch','o/r','master'):'latest'},repository='o/r',include_default_branch=True),[])

    def test_unknown_execution_is_protected_and_current_attempt_is_queried(self):
        import subprocess
        row=dict(run(1,'old',branch='master',event='push'),run_attempt=3)
        with patch.object(cancel,'gh_pages',side_effect=subprocess.CalledProcessError(1,['gh'])):
            self.assertTrue(cancel.master_execution_started('o/r',row))
        with patch.object(cancel,'gh_pages',return_value=[]) as read:
            self.assertFalse(cancel.master_execution_started('o/r',row))
            self.assertIn('/attempts/3/jobs',read.call_args.args[0])
