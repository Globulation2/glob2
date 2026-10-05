# Deployment status race verification

Tested head3da592dbb4243c53549a31459d24875ff7f4fcde, base598524bb0dbad2c796eaa6eb645c15ee54d380ac. Refetched masterf2343768b4deda55d60343dd7648294e1ad149ff before acceptance; its address-pool smoke/config/docs changes do not modify this status command or test driver. No rebase needed.
Ubuntu26.04.1 x86_64, Python3.14.4, POSIX /bin/sh (dash0.5.12), util-linux flock. Scratch-host fixture creates its own temporary Git repositories and fake deployment script; no real host deploy performed.

Commands:
```
python3 artifacts/deploy-status/before-probe.py
python3 -m unittest discover -s test/deployment -v
python3 artifacts/deploy-status/repeat-probe.py
sh -n deploy/online-deploy.sh
```

Before probe substitutes the unchanged base driver into the final regression fixture: exit1, deterministic lost/empty exit/empty rollback instead of done/1/rolled-back. The fake deployment waits for a file gate; a controlled liveness function releases it and waits for the atomic completion marker before returning process-dead. No timing delay or scheduler-speed assumption decides the race.
After: all35deployment tests pass in8.716s, including running/completed/missing status, rollback reporting, mutual exclusion and driver/update-host behavior. Controlled race repeated20times passes in4.085s. POSIX shell syntax passes. The observed earlier random rollback-test failure in address-pool verification follows this same completion/liveness window.

The status query now reads completion after liveness. Actual lost processes remain lost when no exit marker exists; an atomic published completion takes precedence. Deployment execution, locks and rollback actions are unchanged. No simulation/save/replay/protocol changes and noSIM_REVISION bump.
No real SSH host deployment, remote process crash or non-Linux shell execution exercised. Pure status-reporting repair, so native/browser/mobile suites omitted.
Maintainer acceptance: Codex accepts this evidence under AGENTS.md.
