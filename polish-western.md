# Fresh Western translation editing pass

Reviewed all 309 newly added or filled messages per catalog across br, ca, de, dk, en, eo, es, eu, fi, fr, it, nl, pt and sv (4,326 entries). Compared tutorial instructions against English, existing building/control catalog labels and the touch HUD use of Paint/Erase/Pan. English retained: no semantic changes requiring retranslation.

50 targeted value edits in 13 non-English catalogs. The complete before/after evidence is in `polish-western-changes.json`.

| Locale | Edited values |
| --- | ---: |
| br | 3 |
| ca | 1 |
| de | 5 |
| dk | 2 |
| en | 0 |
| eo | 5 |
| es | 2 |
| eu | 6 |
| fi | 1 |
| fr | 3 |
| it | 5 |
| nl | 7 |
| pt | 6 |
| sv | 4 |

Significant corrections:

- Match tutorial menu/control references to actual labels: Portuguese Opções, Dutch Besturing, Esperanto Ludaj kontroloj/Laboranta(j)/Forviŝi/Movi, Danish Afbryd, German Entdeckerflagge.
- Use established guard-area terminology in Spanish, Italian, Portuguese, Brazilian Portuguese, Dutch, Swedish and Basque so users can find the named tool. Articles and gender adjusted with the changes.
- Clarify the MCP controller as the client holding control, rather than hardware controller/control scheme (German, Swedish, Dutch, Italian); give understandable connection-address labels (Italian, Dutch, Portuguese).
- Correct Danish higher-level buildings (previously taller buildings), Esperanto inland (previously inside the earth), French stone/resource quantities and Brazilian wood quantities.
- Replace misleading system-slowness/kinetic-energy wording with scroll inertia in Dutch/Finnish/German. Smooth German save/load and pan instructions and Catalan wood gathering.

Preserved all keys, placeholders, source signatures and identical tutorial part2 messages29/30. No runtime/code edits. Native-speaker certification remains outside this AI review; many already good strings were deliberately retained.
