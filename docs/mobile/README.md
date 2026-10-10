# Mobile development

Build, test and understand the Android and iOS clients.

## Develop

- [Android build and native tests](android.md).
- [iOS build and simulators](ios.md).
- [Pinned tools, isolation and assets](toolchains.md).
- [Responsive presentation policy](interface.md).
- [Touch gameplay controls](gameplay-controls.md).
- [Touch map and campaign editing](editor-interface.md).
- [Verification and design gallery](verification.md).

## Release and policies

- [Store releases](../releases/README.md).
- [Unified website, game and online service privacy policy](privacy-policy.md).
- [Fire tablet privacy section](privacy-policy.md#fire-tablet-edition).

[Documentation index](../README.md).

## Legal publication

`privacy-policy.md` and `terms.md` are the canonical legal documents for the
public website and official online service. The website repository imports both
with `scripts/sync-legal.mjs`; run its `--check --game-repo` command against this
checkout when changing either document. Public pages are
https://glob2online.com/privacy/ and https://glob2online.com/terms/. The old
Amazon privacy file is a compatibility pointer. Publish the website pages
before shipping game links or updating mobile store listings.
