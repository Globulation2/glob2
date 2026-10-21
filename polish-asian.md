# Fresh independent translation editing pass

Reviewed the 309 added/filled values in each of ar, fa, id, ja, ko, vi, zh-cn and zh-tw (2,472 values): 99 general/settings/recording strings, 116 tutorial paragraphs and four mission titles, and 90 library/overlay strings. Read against current English and actual localized control labels (Working, Controls, Message history, Flags, Guard, Paint, Erase, Pan, Build, Cancel, Upgrade, Repair, OK). Arabic/Persian presentation forms were normalized only in temporary review output; existing catalog text was not globally normalized or regenerated.

Applied 71 targeted edits, preserving the remaining translations and all catalog keys:

| Locale | Edits | Examples |
| --- | ---: | --- |
| Arabic | 8 | Collapse-details action replaces literal “Less”; CPU fallback label explicitly counts occurrences; repair tutorial names the actual repair action; feeding capacity states simultaneous service; update/clipboard instructions clarified. |
| Persian | 14 | Wheel help now includes zooming out as well as zooming in; Space means the keyboard key rather than an unqualified space; first tutorial restores disappearing-message/history explanation; release terminology is consistent with version labels; collapse/details and Fit actions clearer. |
| Indonesian | 12 | Guard tutorial now names actual “Jaga” UI control, rather than “Penjagaan”; unlisted families are described as absent from the list rather than unregistered; completed Inn operating assignment and fruit instruction grammar repaired; generator terminology consistent. |
| Japanese | 6 | Tutorial now explicitly tells player to move the view rather than claiming narrator will move it; worker cost clearly refers to producing a new worker; Inn operating assignment rewritten naturally; thumb target described as a button. |
| Korean | 7 | Defense preparation explicitly precedes the enemy's attack; School construction and swimming prerequisite clarified; thumb control and water-only labels more natural; reroll/check status wording improved. |
| Vietnamese | 16 | Running-track completion no longer grammatically reads as “finished training”; trained workers/construction prerequisites clearer; defense instruction explicitly waits for enemy attack; release/package identity uses technical identification information rather than personhood; several terse/awkward tutorial sentences expanded naturally. |
| Simplified Chinese | 5 | Package/release identity uses 标识 instead of 身份; Repair tutorial names actual 修理 button; reroll action clearer. |
| Traditional Chinese | 3 | Explicit-update message now describes intentional user update naturally; landscape preview instructions clarify adjustable map size/colony count; reroll action clearer. |

Seed-change labels avoid implying a particular random-number distribution. No game rules, counts, directions or target values changed. Pool tutorial paragraphs 29/30 remain exactly equal in every owned locale, retaining legacy alias requirements. A few translations were already good enough and were retained rather than needlessly rewritten.

Validation: all 71 final values match the keyed edit log, English %N placeholder multisets match, all eight Pool29/30 equality checks pass, and scoped git diff --check passes. Exact before/after records: second-asian-edit-log.json; structural results: second-asian-validation.json. Parent owns final strict catalog audit/build/run checks. No build, commit, font change, or source-code modification performed by this editor.

Limits: independent model linguistic/semantic review, not native-speaker certification. No fresh rendered layout inspection in this pass. Historical catalog strings outside the 309 added/filled entries per locale were consulted for terminology but not comprehensively proofread.
