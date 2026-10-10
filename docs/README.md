# Documentation

Use these topic guides to understand, change, create content for, and operate
Globulation 2. Each index orders its tutorials, procedures, explanations, and
references so you can start with an overview and follow the task you need.

## Topics

| Topic | Start here |
| --- | --- |
| [Development](development/README.md) | Setup, builds, tests, contribution, dependencies, troubleshooting |
| [Architecture](architecture/README.md) | Engine responsibilities, simulation, rendering, persistence, compatibility |
| [Features](features/README.md) | Gameplay and interface behavior, supported experiments |
| [AI](ai/README.md) | Opponents, implementation, configuration, evaluation, telemetry |
| [Map generators](map-generators/README.md) | Design, authoring, framework, controls, generator catalog |
| [Assets](assets/README.md) | Artwork, terrain, music, production workflows, provenance |
| [Scripting](scripting/README.md) | Script authoring, execution model, APIs |
| [Multiplayer](multiplayer/README.md) | Platform, identity, clients, rooms, relay, protocols |
| [Browser](browser/README.md) | Browser development, storage, rendering, transports, decisions |
| [Mobile](mobile/README.md) | Android/iOS development, interaction requirements, verification |
| [Hosting](hosting/README.md) | Local setup, production configuration, maintenance, recovery |
| [Releases](releases/README.md) | Packaging, distribution, store procedures |
| [Tools](tools/README.md) | Contributor tools and workflows across topics |

## Reader paths

- **Contributors:** [contribution guide](../CONTRIBUTING.md), development, architecture,
  then the relevant subsystem. [Repository instructions](../AGENTS.md) record the
  shared compatibility and review policies.
- **Content creators:** assets, map generators, and scripting; feature references
  explain the behavior your content can configure.
- **Operators:** hosting, multiplayer, and releases.
- **Players:** [public guides and downloads](https://glob2online.com/) and the
  [browser game](https://app.glob2online.com/). In-repo feature pages explain current
  capabilities and experimental controls.

## Maintain the library

Follow the [documentation maintenance guide](development/documentation.md).
[Licensing](development/licensing.md) indexes code and asset notices.
[Support](../SUPPORT.md) explains how to report a problem.

Keep temporary notes and draft narratives in ignored `docs/.work/`; keep generated
review evidence in ignored root `artifacts/`. Attach evidence needed for review to
the pull request or an accessible evidence branch. Git history preserves retired
designs and reports; current guides describe maintained behavior.
