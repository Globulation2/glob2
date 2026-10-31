# Marathi localization evidence

Feature revision: `6e376b299948b57e3d4e6f22673b4b0acc0730c4` (application checks at parent `c9d772635398c57582069b58208eb337cee2787b`; the follow-up changes only the pending browser-game test).

This is an evidence branch, not a source or release branch. All evidence is agent-reviewed, not native-human-certified.

* Complete Marathi: 3,393 game keys and 2,836 online keys, including plural forms.
* Actual Google NMT requests used project `bradley-personal-20261027`. The ledger conservatively reserves US$3.86948 before free credits, against a US$9 ceiling. Cached responses and failed-token diagnostics are retained.
* Three independent reviewers cover every key with semantic, editorial and adversarial passes; final changes were reread. Signed source/catalog/batch hashes and explicit unchanged-value carry proofs are included.
* Native final SHA: `8b3f9e4972ee0eb1c3433110a7f0e83c9dc2fb7ea5f494b6925afa373c4fef97`. Online final SHA: `0a35ecc3e76fd00b68db89ca5dd18967304524eb7c3c0b8cc81c37b70e5694a4`.
* Strict native and online contracts, mocked drafting tests, platform/API tests, type checks and desktop/phone web UI evidence are included in the corresponding logs and registration-checks.json. A transient Chromium capture failure was retained and its affected case rerun; the final six cases passed across those runs.
* Small-font images are direct SDL_ttf diagnostics at 10/13/20px, not game-screen certification. They use the pinned bundled font and HarfBuzz10.1.0.

At this checkpoint, full native screen matrices, serial/threaded WASM runtime checks and the Android release package/rendering checks are still running. Their gates remain pending in reviews/mr.json. macOS, Windows and iOS execution has not been verified. This branch will be refreshed as actual evidence arrives.

Source/reference snapshots from external websites are deliberately excluded. The language brief retains citations, short consulted excerpts and local snapshot hashes. Full historical review JSONs refer to earlier stages; final signoff files identify the approved bytes.
