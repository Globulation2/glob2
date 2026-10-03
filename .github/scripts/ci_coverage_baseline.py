"""Read inert master selection evidence; never execute downloaded artifacts."""
import io
import json
import os
import subprocess
import zipfile
from ci_run_metrics import api, run_artifacts


def selection_evidence(repo, run, token, read=api):
    if (run.get('name') != 'build' or run.get('head_branch') != 'master' or
            run.get('conclusion') != 'success' or
            run.get('event') not in ('push', 'schedule', 'workflow_dispatch')):
        return None
    prefix = f'repos/{repo}/actions/runs/{run["id"]}'
    for artifact in run_artifacts(prefix, token, read):
        if artifact['name'] != 'ci-observation-selection' or artifact['expired']:
            continue
        data = read(f'repos/{repo}/actions/artifacts/{artifact["id"]}/zip', token, True)
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for member in archive.infolist():
                if member.filename.endswith('ci-selection.json') and member.file_size <= 1024 * 1024:
                    observed = json.loads(archive.read(member))
                    if observed.get('sha') == run['head_sha']:
                        return observed
    return None


def validated_baseline(repo, run_id, token, read=api):
    if not run_id or not str(run_id).isdigit():
        return False
    try:
        from ci_policy import FLAGS, fingerprint
        run = read(f'repos/{repo}/actions/runs/{run_id}', token)
        run = dict(run, id=run_id)
        observed = selection_evidence(repo, run, token, read)
        return bool(observed and observed.get('full_matrix') is True and
                    observed.get('policy_fingerprint') == fingerprint() and
                    all(observed.get('selection', {}).get(k) is True for k in FLAGS))
    except (OSError, ValueError, KeyError, zipfile.BadZipFile):
        return False


def recent_successes(repo, token, read=api):
    return read(f'repos/{repo}/actions/workflows/build.yml/runs?branch=master&status=success&per_page=100', token)['workflow_runs']


def activated():
    if os.environ.get('CI_TIERED_COVERAGE_ENABLED') != 'true':
        return False
    repo, token = os.environ.get('GITHUB_REPOSITORY', ''), os.environ.get('GH_TOKEN', '')
    run_id = os.environ.get('CI_TIER_BASELINE_RUN_ID')
    if run_id and validated_baseline(repo, run_id, token):
        return True
    # New successful full runs refresh the gate without an expiring manual pointer.
    try:
        return any(validated_baseline(repo, run['id'], token) for run in recent_successes(repo, token)[:20])
    except (OSError, ValueError, KeyError):
        return False


def master_checkpoint(repo, token, read=api):
    """Newest successful ancestor under this policy; missing evidence means full."""
    from ci_policy import fingerprint
    policy = fingerprint()
    try:
        for run in recent_successes(repo, token, read):
            if run.get('event') not in ('push', 'schedule', 'workflow_dispatch'):
                continue
            # Full-history checkout makes skipped merges visible. Never use a
            # divergent/future success or a browser-only/manual partial run.
            sha = run['head_sha']
            ancestor = subprocess.run(['git', 'merge-base', '--is-ancestor', sha, 'HEAD'], capture_output=True)
            if ancestor.returncode:
                continue
            observed = selection_evidence(repo, run, token, read)
            if (observed and observed.get('policy_fingerprint') == policy and
                    observed.get('checkpoint_eligible') is True):
                return sha
    except (OSError, ValueError, KeyError, zipfile.BadZipFile):
        pass
    return None


def successful_full_run(repo, sha, token, read=api):
    """Reuse only available full hosted evidence for the exact revision and policy."""
    if not repo or not sha or not token:
        return None
    try:
        from ci_policy import FLAGS, fingerprint
        policy = fingerprint()
        for run in recent_successes(repo, token, read):
            if run.get('head_sha') != sha:
                continue
            observed = selection_evidence(repo, run, token, read)
            if (observed and observed.get('full_matrix') is True and
                    observed.get('policy_fingerprint') == policy and
                    all(observed.get('selection', {}).get(flag) is True for flag in FLAGS)):
                return run['id']
    except (OSError, ValueError, KeyError, TypeError, AttributeError, EOFError, zipfile.BadZipFile):
        pass
    return None
