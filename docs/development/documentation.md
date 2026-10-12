# Maintaining documentation

Use this guide when adding, changing, moving, or retiring repository documentation.
The [documentation index](../README.md) defines the topic categories; each topic
owns its current behavior and procedures.

## Choose a canonical home

Write for a named reader and one purpose. Put the maintained explanation in its
topic directory and link it from that topic's index. Keep colocated READMEs short:
explain the directory, its important files, and the canonical guides. Link shared
contracts instead of repeating them.

Use lowercase kebab-case filenames, `README.md` for indexes, one H1 title, and
ordered H2/H3 sections. Preserve externally consumed paths, legal documents,
symlink relationships, and generator-owned filenames. Split a page when its tasks
or audiences diverge, not because it exceeds a fixed line count.

## Write for the task

Open with the purpose and expected result. Add prerequisites only when needed.
Use these structures as appropriate rather than filling empty template sections:

| Type | Useful sequence |
| --- | --- |
| Tutorial | Goal, prerequisites, steps, expected result, next task |
| Procedure | When to use it, prerequisites, actions, verification, recovery |
| Reference | Contract or interface, fields/options, constraints, examples |
| Explanation | Concept, responsibilities, behavior, tradeoffs, related guides |
| Decision | Status, context, decision, consequences, current references |
| Index | Orientation, ordered paths, focused references |

Explain acronyms and consequences. Keep examples minimal and reproducible. Mark
experimental capabilities by their current support and limitations, not by an old
implementation milestone. State historical version transitions explicitly when
they remain necessary to understand compatibility.

## Check accuracy

Read the implementation, scripts, configuration, and tests behind each changed
section. Check commands against current help or safe execution; review production
and destructive commands statically. Distinguish guarantees, current defaults,
examples, and design recommendations. Update related guides together if a contract
changes. A link checker cannot establish factual accuracy.

Generated references must name their generator and validation command. Change the
generator/input and regenerate when needed; do not manually rewrite its output.

## Validate links and navigation

Install the pinned contributor environment described in `requirements-dev.txt`,
or use an isolated documentation environment:

```sh
python3 -m venv artifacts/docs-venv
artifacts/docs-venv/bin/python -m pip install -r tools/docs/requirements.txt
artifacts/docs-venv/bin/python -m unittest discover -s test -p test_check_docs.py -v
artifacts/docs-venv/bin/python tools/check_docs.py
```

The checker parses Markdown and checks local links, images, GitHub-style heading
anchors, explicit HTML anchors, topic order, and reachability from the main index.
It ignores fenced examples. Named exceptions in `tools/docs/navigation.json` explain
vendored, fixture, legal, and generated handling; do not add exceptions to hide
broken maintained links. Duplicate headings use GitHub's numbered anchor suffixes.

Cheap CI runs the deterministic checks. External destinations can be audited
separately with `python3 tools/check_docs.py --external` in an environment containing
the pinned dependencies. That network-dependent audit can report authentication,
rate-limit, or transient failures; inspect results before changing a valid link.

## Move or retire material

Update references throughout the repository, including source comments, workflows,
agent skills, and tool READMEs. Check links to sections as well as files. Do not
leave forwarding pages for retired guides. Preserve the published mobile privacy
policy paths embedded in the game.

Extract useful guarantees, limitations, and reproduction procedures before deleting
obsolete plans or reports. Git history is the historical record. Put temporary
migration ledgers and review drafts in ignored `docs/.work/`; put logs, maps, saves,
replays, screenshots, and other evidence in ignored root `artifacts/`. Publish
review evidence through accessible PR attachments or an evidence branch, and retain
only durable conclusions in the maintained guide.
