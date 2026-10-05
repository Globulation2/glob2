# Backend address pool verification

Tested head eed8251e4da808469c4b2c6ea1c0fe438d40ac2c, base598524bb0dbad2c796eaa6eb645c15ee54d380ac. Fetched master after validation; unchanged.
Ubuntu26.04.1 x86_64, Python3.14.4, Docker29.1.3, Compose2.40.3. Repository Dockerfile/locked dependencies, new isolated tag and fresh build (compatible cache used), eight build jobs. Actual subnet/pool values materialized with Docker Compose config before real network allocation, not mocked allocation.

Commands:
```
python3 artifacts/backend-pool/probe.py
python3 test/deployment/platform_stack_smoke.py --tag ci-smoke-backend-eed8251e4 --jobs 8 --log-dir artifacts/backend-pool/stack --match-e2e
python3 -m unittest discover -s test/deployment -v
python3 -m unittest discover -s test/build_system -p test_ci_policy.py -v
```

Docker probe exit0: allocate12 real dynamic endpoints before connecting the fixed .10 proxy. Old Compose config gives .10 to a dynamic endpoint and rejects proxy with Address already in use. Final config gives dynamic endpoints .129..140 and accepts .10. All owned probe containers/networks cleaned up. Full isolated stack all9checks pass including worker health, routing, trusted relay path, generation, rated relay match, verified record and rating updates. Project and its disposable volumes cleaned up. CI policy19pass. Deployment first34run failed one rollback status test on an unrelated completion/liveness race; repeated34run passed. Separate focused status repair has a deterministic regression test and is being prepared; no failure suppressed.

The production pool defaults to172.30.89.128/25; custom subnets must configure a matching GLOB2_BACKEND_IP_RANGE excluding the fixed proxy. Existing backend networks require coordinated recreation without deleting data volumes, documented in docs/hosting/README.md. No production stack was modified in this validation.

No simulation/save/replay/protocol change; noSIM_REVISION bump. Native game matrix, other container runtimes/platforms, scale above12 endpoints and production network migration not exercised. Local stack verifies this network repair; does not establish fullmaster green.
Original CI failure https://github.com/Globulation2/glob2/actions/runs/37276118218/job/111655619536
Maintainer acceptance: Codex accepts this focused evidence for the address allocation change under AGENTS.md.
