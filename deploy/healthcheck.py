#!/usr/bin/python3
"""Private process liveness or dependency-aware readiness; never probes raw TCP."""
import sys
import urllib.request
role = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else 'ready'
port = 7493 if role == 'router' else 7492
route = '/livez' if mode == 'live' else '/readyz'
with urllib.request.urlopen(f'http://127.0.0.1:{port}{route}', timeout=2) as response:
    assert response.status == 200
