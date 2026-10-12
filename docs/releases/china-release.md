# Mainland China release

Release workflow and verification requirements.

Government game approval and storefront acceptance are separate steps for a
mainland China release. A submitted application, foreign store listing, or free
price does not authorize a mainland download.

## Release configuration

Build the local-play client with `scons china=1 release=1`. The China identity
uses its own build directory. The Android and iOS targets also accept
`china=1`; see [mobile builds](../mobile/development.md) for toolchains and
packaging. Pass `--china` to `mobile/dependencies.py` and the matching
`mobile/android.py` or `mobile/ios.py` command for every mobile stage. The China
client omits the main menu's Play online entry and never opens the online hub,
including from invite links. Offline play and LAN sessions remain available. This build mode is a technical release candidate, not
proof of approval or completion of required identity and anti-addiction controls.

The publisher and operator must review LAN play, editable maps, scripts, chat,
and all included content. Record the reviewed feature set and build hashes.
Ask the publisher whether subsequent changes need an amendment or new review.

Run `python3 tools/china/export_review_text.py --output artifacts/china/text-review.tsv`
to inventory Simplified Chinese UI strings and tutorial dialogue. Add `--strict`
to fail while the tutorial has untranslated messages. Review campaign names,
maps, and other embedded text separately; the export does not claim to be the
complete NPPA script until that review is finished.

## Approval sequence

1. Document the legal rights holder's authority over code, art, music,
   translations, name, and bundled content. Have counsel check how the game's
   open-source licenses interact with store terms and the publishing agreement.
2. Select a mainland publishing unit qualified for online games and an operator
   with the required ICP license. Verify their credentials and agree on rights,
   territory, platforms, term, storefront responsibilities, and reporting.
3. Have the publisher confirm the Chinese title, imported-game classification,
   and whether desktop and mobile can be covered in one application. Register
   the applicable copyright authorization as required.
4. Freeze reviewable builds and assemble the NPPA materials. The publisher
   submits through its provincial publishing authority. Track acceptance,
   correction requests, approval, and the ISBN and approval number.
5. Complete each storefront's onboarding and applicable mobile app filing
   before making the game available in mainland China.

NPPA's [imported online game requirements](https://www.nppa.gov.cn/bsfw/xksx/cbfxl/wlcbfwspsx/202210/t20221013_600723.html)
describe the filing path and an 80-working-day period from acceptance.
Preparation and requested revisions take additional time.

## Submission inventory

Keep signed contracts and personal data in controlled storage, not this
repository. The publisher owns the official filing; this is a preparation
checklist for Globulation 2.

| Material | Evidence to prepare |
| --- | --- |
| Rights and entities | Rights chain, registered authorization, publisher qualifications, operator business and ICP licenses |
| Game description | Chinese title, platforms, gameplay, and overseas release history |
| Content review | Complete Chinese in-game text, content inventory, applicable prohibited-word list, publisher review record |
| Visuals | At least ten clear screenshots including the title screen |
| Builds | Reviewable desktop and mobile packages, installation instructions, hashes, and all assets |
| Controls | Applicable identity and anti-addiction design, test accounts, and operator evidence |
| Video | At least ten minutes covering title, main screen, systems, scenes, and actual combat |

The [NPPA application list](https://www.nppa.gov.cn/bsfw/xksx/cbfxl/wlcbfwspsx/202210/t20221013_600723.html)
specifies forms and media. The publisher must determine which controls apply
to the reviewed local-play configuration; free distribution alone does not
establish an exemption.

## Store and release gates

Steam China requires government approval and works with selected partners;
contact Steamworks with the AppID once a publisher is engaged. Apple requires
the approval number and supporting papers for its mainland App Store and
checks applicable ICP filing information. TapTap has its own qualification
and review steps for mobile and PC distribution. Other mainland Android
stores are a later channel after the initial release is stable.

Before publishing, confirm that approval covers each platform and version,
store listings match the approved title and materials, the operator has
completed applicable filings, and packages contain the reviewed content.
Follow the [development reference](../development/README.md) and
[mobile verification](../mobile/development.md) for technical checks.
Game-feel changes need maintainer play review, and substantive gameplay
changes need independent human maintainer approval under `AGENTS.md`.

Sources: [NPPA application](https://www.nppa.gov.cn/bsfw/xksx/cbfxl/wlcbfwspsx/202210/t20221013_600723.html),
[NPPA anti-addiction notice](https://www.nppa.gov.cn/xxfb/tzgs/202108/t20210830_666285.html),
[Steam China](https://partner.steamgames.com/doc/store/china?l=english),
[Apple App Store Connect](https://developer.apple.com/help/app-store-connect/reference/app-information/app-information/),
[TapTap developer guide](https://developer.taptap.cn/docs/store/standardies-operation/).

[Release index](README.md) · [Documentation index](../README.md).
