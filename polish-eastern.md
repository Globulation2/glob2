# Independent editorial pass: eastern and central Europe

Reviewed the newly added/filled native catalog entries against English and established locale terms, including all 116 tutorial messages, mission titles, settings, shared libraries, recording, Hive, Discord, and movement statistics. Reviewed 11 assigned languages. Changed values only in the assigned catalogs; no keys or control flow changed.

| Locale | Edited values |
| --- | ---: |
| cz | 4 |
| gr | 10 |
| hu | 15 |
| pl | 11 |
| ro | 9 |
| ru | 16 |
| si | 17 |
| sk | 12 |
| sr | 11 |
| tr | 6 |
| uk | 14 |

Total: 125 edited values. Counts compare against the catalogs at the start of this editorial pass, not HEAD.

Meaningful corrections:

- Greek: Space instructions now name the keyboard's blank/space key instead of the word for an interval. Damaged buildings are described as having damage, avoiding a claim that destroyed buildings can be repaired. Rewrote the approximate-location scouting instruction naturally and restored the explicit defensive purpose of towers and walls.
- Hungarian: replaced invented/literal worker-goal compounds with the desired number of assigned workers, clarified separate operating/building assignments, and replaced familial adjectives with references to building families.
- Russian, Ukrainian, Polish, Slovak, and Slovenian: made worker-target instructions refer to a desired count, retained construction vs supply distinctions, and clarified that tower worker counts increase rather than changing a vague goal.
- Slovenian: aligned new tutorial warrior references with the established HUD noun "vojaki", including singular/plural forms and the mission title.
- Serbian: fixed the construction meaning of "need", food-needs grammar, repair-command sentence structure, and resource counts to say units of wood/stone rather than numbers of trees or material types.
- Romanian: fixed CPU adjective agreement, clarified scalar computation vs processor, used resource units, and made the protected inland School location unambiguous.
- Turkish: clarified which worker brings wheat, restored "better" training in upgrade benefits, and improved the scouting location sentence and supply comparison.
- Czech: corrected "higher buildings" to buildings of higher levels, made tower worker assignment explicit, and simplified update/replacement wording.
- Across several locales: replaced unnatural literal "explicit update" phrasing with a clear optional update instruction; clarified that an installed package has a given ID, rather than claiming that an ID itself was installed. CPU fallback descriptions refer to automatic computation mode, and comparison descriptions identify scalar calculation results rather than a physical scalar CPU.

Local integrity verification: all catalog key sequences unchanged; all edited placeholder multisets unchanged; pool tutorial messages 29/30 remain identical in all 11 locales. Parent runs the final strict source/catalog and font checks. No builds, commits, or generated alias changes made by this editor.

Limitations: this is independent AI editing, not native-speaker certification. Existing legacy translations outside the current change were used as terminology context and generally left untouched. No new claims of native platform coverage.
