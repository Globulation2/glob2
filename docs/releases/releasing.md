# Release packaging

Release workflows run from the restricted release mirror. Start with [release qualification](downloads.md), then choose your target in the [release index](README.md).

## The release mirror

Release pipelines run only in the release mirror, the owner-controlled public
`genixpro/glob2-release` repository. Every release, publication and deployment
workflow is defined here, in `Globulation2/glob2`, but each of its root jobs
has a job-level `if:` that requires the mirror's name and numeric repository ID
(`1397722696`), plus the owner and the expected ref. Anywhere else, including
this repository and forks, every job is skipped before a runner starts. None of
these workflows has a pull request trigger.
`test/build_system/test_release_guards.py` enforces this for every workflow in
the release set in `test/build_system/test_ci_concurrency.py`, and fails if a
workflow that uses deployment environments, named secrets or write tokens is
missing from that set. Add a new release workflow to that set and give its root
jobs the same guard.

The mirror's `master` has exactly the same files as `Globulation2/glob2`
`master`: each sync is a merge commit whose tree is upstream's, and the mirror
carries no changes of its own. The mirror's older history has its own merge
commits, so syncs cannot be fast-forwards, and its `master` refuses force
pushes. A change needed
for releasing, including a workflow or packaging fix, is made here first,
through a normal pull request, and reaches the mirror with the next sync.
Configure release credentials only as restricted GitHub environment secrets in
the mirror.

Keep the release mirror public and owner-controlled: disable pull requests,
issues, projects, wiki and discussions; leave `genixpro` as its only
collaborator; restrict creation, updates and deletion of every mirror branch to
that user, while `master` also blocks force pushes and deletion. Restrict Actions
execution to `genixpro`, allow only the pinned actions
needed by the mirror workflows, and keep the default `GITHUB_TOKEN` read-only.
Enable secret scanning and push protection. Public repositories remain readable
and forkable, so never put credentials in code, workflow inputs, logs or
artifacts. Only the mirror's restricted environments hold release credentials.


[Release index](README.md) · [Documentation index](../README.md).
