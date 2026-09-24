# Documentation

The documentation tree contains deliberately curated, durable project guides. A
file is not documentation merely because it was written during development. It
belongs here only when it has a clear long-term audience and will be maintained
with the behavior it describes. Temporary development notes, generated evidence,
dated reports and pull-request artifacts do not belong here.

## Topics

- **AI:** [telemetry](ai/telemetry.md), [gameplay measurements](ai/gameplay-statistics.md),
  [Cortex mechanics](ai/cortex-upgrade-expand-mechanics.md), and [Maxima](ai/maxima/README.md).
- **Assets:** [third-party attribution](assets/source-attribution.md) and
  [high-resolution artwork provenance](assets/high-resolution/README.md).
- **Development:** [build and coding reference](development/reference.md),
  [headless replays](development/headless-replays.md),
  [performance telemetry](development/performance-telemetry.md),
  [save continuation](development/savegame-continuation.md), and the
  [historical architecture overview](development/legacy-architecture.txt).
- **Features:** [custom-game setup](features/custom-game-setup/README.md),
  [map previews](features/pre-game-map-preview.md),
  [window resizing](features/window-resizing.md), and the
  [toroidal view](features/torus-experiment.md).
- **Map generators:** [design and implementation index](map-generators/README.md).
- **Tools:** [distributed tournaments](tools/tournaments.md).

- **Browser platform:** [build and play](../browser/README.md),
  [architecture](browser/implementation.md), [storage](browser/storage.md),
  [viewport](browser/viewport.md), and [YOG deployment](browser/gateway.md).

## Temporary work

Use the ignored root `artifacts/` directory for screenshots, logs,
maps, saves, replays, datasets, profiles and archives. Use the ignored `.work/`
directory beside this file for temporary Markdown, validation narratives and PR
drafts. Neither location is committed.

Evidence required for review should be attached to the pull request or stored on a
dedicated evidence branch. Preserve only conclusions that remain useful after the
change merges, and add those conclusions to the appropriate durable guide above.

- [AI ratings](ai/ratings.md): measured opponent strength and interpretation.
