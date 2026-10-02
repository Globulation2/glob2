"""Require a successful full master run before activating reduced PR matrices."""
import io
import json
import os
import zipfile
from ci_run_metrics import api


def validated_baseline(repo, run_id, token, read=api):
    if not run_id or not str(run_id).isdigit():
        return False
    prefix=f'repos/{repo}/actions/runs/{run_id}'
    try:
        run=read(prefix,token)
        if run.get('name')!='build' or run.get('head_branch')!='master' or run.get('conclusion')!='success' or run.get('event') not in ('push','schedule','workflow_dispatch'):
            return False
        for artifact in read(prefix+'/artifacts?per_page=100',token)['artifacts']:
            if artifact['name']!='ci-observation-selection' or artifact['expired']:continue
            data=read(f'repos/{repo}/actions/artifacts/{artifact["id"]}/zip',token,True)
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                for member in archive.infolist():
                    if member.filename.endswith('ci-selection.json') and member.file_size<=1024*1024:
                        observed=json.loads(archive.read(member))
                        return observed.get('full_matrix') is True and observed.get('sha')==run['head_sha'] and all(observed.get('selection',{}).get(k) is True for k in ('native','browser','map_generators','deployment','cross_platform','android'))
    except (OSError,ValueError,KeyError,zipfile.BadZipFile):
        return False
    return False


def activated():
    return os.environ.get('CI_TIERED_COVERAGE_ENABLED')=='true' and validated_baseline(os.environ.get('GITHUB_REPOSITORY',''),os.environ.get('CI_TIER_BASELINE_RUN_ID'),os.environ.get('GH_TOKEN',''))
