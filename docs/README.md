# Documentation

The documentation tree contains deliberately curated, durable project guides. A
file is not documentation merely because it was written during development. It
belongs here only when it has a clear long-term audience and will be maintained
with the behavior it describes. Temporary development notes, generated evidence,
dated reports and pull-request artifacts do not belong here.

## Topics

- **Globulation 2 Online website:** [Astro website repository](https://github.com/Globulation2/glob2-online-website),
  [content and legacy migration](https://github.com/Globulation2/glob2-online-website/blob/main/docs/content.md),
  and [Firebase deployment, CI, and rollback](https://github.com/Globulation2/glob2-online-website/blob/main/docs/hosting.md).
  The public site is [glob2online.com](https://glob2online.com/); the browser game
  and multiplayer app use [app.glob2online.com](https://app.glob2online.com/).
- **AI:** [telemetry](ai/telemetry.md), [gameplay measurements](ai/gameplay-statistics.md),
  [Cortex mechanics](ai/cortex-upgrade-expand-mechanics.md), and [Maxima](ai/maxima/README.md).
- **Assets:** [third-party attribution](assets/source-attribution.md) and
  [high-resolution artwork provenance](assets/high-resolution/README.md).
- **Development:** [build and coding reference](development/reference.md),
  [Mac App Store release](development/mac-app-store.md),
  [mainland China release](development/china-release.md),
  [menus and dialogs on the declarative UI framework](development/ui-framework.md),
  [release packaging](development/releasing.md),
  [headless replays](development/headless-replays.md),
  [JavaScript scripting](development/javascript.md) and
  [API reference](development/javascript-api.md),
  [performance telemetry](development/performance-telemetry.md) and
  [network telemetry](development/network-telemetry.md),
  [save continuation](development/savegame-continuation.md), and the
  [historical architecture overview](development/legacy-architecture.txt).
- **Features:** [gameplay footage and automatic chapters](features/gameplay-recording.md), [custom-game setup](features/custom-game-setup/README.md),
  [experimental features](features/experimental-features.md) and the
  [guard-area balancing](features/guard-area-balancing.md) experiment,
  [map previews](features/pre-game-map-preview.md),
  [window resizing](features/window-resizing.md), and the
  [toroidal view](features/torus-experiment.md).
- **Map generators:** [design and implementation index](map-generators/README.md).
- **Online multiplayer:** [platform architecture](multiplayer/architecture.md),
  [identity and sign-in](multiplayer/identity.md),
  [rooms and matches](multiplayer/rooms-and-matches.md),
  [ratings and matchmaking](multiplayer/ratings-and-matchmaking.md),
  [match history and the web app](multiplayer/history-and-web.md),
  [connection quality](multiplayer/connection-quality.md) (Ping, Delay, Behind), the
  [relay-sequenced turn protocol](multiplayer/turn-protocol.md), the
  [match relay](multiplayer/relay.md) that hosts it, [LAN games](multiplayer/lan.md)
  and the [LAN playtest guide](multiplayer/lan-playtest.md).
- **Hosting:** [self-hosting an online instance](hosting/README.md) with the
  Compose stack in `deploy/` (file index: [deploy/README.md](../deploy/README.md)).
- **Tools:** [distributed tournaments](tools/tournaments.md).

- **Mobile platforms:** [builds, responsive UI and verification](mobile/development.md),
  the [privacy policy](mobile/privacy-policy.md) for the Android and iOS apps and the
  official online service, and the
  [Amazon Fire tablet privacy policy](mobile/amazon-privacy-policy.md).

- **Online multiplayer:** [online client](multiplayer/client.md): platform
  connection, sign-in, instances, map cache and invite links.

- **Browser platform:** [build and play](../browser/README.md),
  [architecture](browser/implementation.md), [storage](browser/storage.md),
  [viewport](browser/viewport.md), and [secure network transports](browser/gateway.md).

## Temporary work

Use the ignored root `artifacts/` directory for screenshots, logs,
maps, saves, replays, datasets, profiles and archives. Use the ignored `.work/`
directory beside this file for temporary Markdown, validation narratives and PR
drafts. Neither location is committed.

Evidence required for review should be attached to the pull request or stored on a
dedicated evidence branch. Preserve only conclusions that remain useful after the
change merges, and add those conclusions to the appropriate durable guide above.

- [AI ratings](ai/ratings.md): measured opponent strength and interpretation.
