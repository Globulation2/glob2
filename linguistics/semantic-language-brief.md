# Bulgarian semantic/gameplay brief

Role: independent semantic/gameplay reviewer. This is AI linguistic review, not human certification. Preliminary research and first-60 draft inspection only; full-catalog review has not yet occurred.

Use standard contemporary Bulgarian. Address the player with plural imperative forms: Изберете, Отворете, Поставете, Задайте. Prefer short noun phrases on buttons/settings; do not turn automatic setting descriptions into commands to move things manually. Preserve placeholders, technical identifiers, code and proper names. Bulgarian labels use sentence capitalization rather than English title case. Do not add game mechanics absent from English.

## Working glossary and meaning

| English | Bulgarian | Semantic condition |
|---|---|---|
| unit | единица | A game creature, never устройство/уред. Quantities precede the noun. |
| glob / globule | глобула | Fictional autonomous creature; do not introduce a new species. |
| worker | работник; работници | Collects, builds and supplies; worker targets request available workers, not proof they are present. |
| explorer | разузнавач; разузнавачи | Scout and combat-capable creature, not academic researcher. |
| warrior | воин; воини | Keep the recommended воин spelling consistently. |
| swarm | рояк; рояци | Named building that consumes wheat and creates units, not an attacking mass of units. Slavic catalogs support this name; French/German favor hive/stock metaphor. |
| inn | хан; ханове | Feeds units; imports wheat via suppliers, makes meals. Avoid inventing beds/rest mechanics. |
| supplier | снабдител | Worker gathering and carrying operating resources, distinct from builder. |
| worker target | заявен брой работници | Requested staffing, not actual staffed count; target is not a combat objective. |
| production ratio | съотношение на създаваните единици | Relative proportions of workers/explorers/warriors, not production speed or percentage guarantee. |
| exploration flag | разузнавателен флаг | Requests explorers in area; does not instantly relocate them. |
| war flag | боен флаг | Requests warriors in area. |
| clearing flag | флаг за разчистване | Clears resources; not user data cleanup. |
| team | отбор | A gameplay side; distinguish team from colony/colonists. |
| colony | колония | Economic/simulation colony; not geological deposit. |
| conversion / convert | преминаване към друг отбор / привлечена единица | Attraction through food/fruit, no religion. |
| vision | видимост | Shared map visibility, not eyesight or aesthetic vision. |
| fruit | плодове | Separate from ordinary food/meals, used for conversion and market exchange. |
| replay | повторение | Recorded simulation playback; keep consistent with native menu. |
| room | стая | Multiplayer waiting/playing room. |
| match | мач | Distinguish saved game and live match where meaningful. |
| account | акаунт | Profile can be профил for visible personal page, not indiscriminately interchangeable. |
| release | издание | Published immutable package version; release action may be публикуване. |
| credit | кредит | Purchased studio balance; credits/acknowledgments instead автори/приноси where applicable. |
| terrain | терен | Ground type; terrain credit means studio currency, not land attribution. |
| licence | лиценз | Legal sharing licence; avoid certificate/purchase confusion. |

## Primary Bulgarian references actually read

Accessed 2026-10-31. These are linguistic references, not evidence of any human review or endorsement. Summaries below are paraphrases.

- IBL BAS RBE, [рояк](https://ibl.bas.bg/rbe/lang/bg/рояк/): entry identifies a collective group of bees and broader groups; gives plural рояци and count рояка. Supports a natural Bulgarian collective label, while the game’s building sense is a project terminology decision.
- IBL BAS RBE, [работник](https://ibl.bas.bg/rbe/lang/bg/работник/): entry gives ordinary plural работници and physical/work-doing meanings. Supports worker label; autonomous staffing behavior comes from game text/code, not dictionary.
- IBL BAS RBE/RSCBKE, [хан](https://ibl.bas.bg/rbe/lang/bg/хан/): entry separates lodging/eating establishment from ruler homonym and supplies ханове/хана. Supports concise inn label; food production behavior comes from game.
- IBL BAS advice, [воин/войн](https://ibl.bas.bg/ezikovispravki/vaprosi/pravopis-na-otdelni-dumi-i-formi-flumaster-simultanen-2/286): both spellings accepted, воин recommended.
- IBL BAS advice, [agreement in polite address](https://ibl.bas.bg/ezikovispravki/vaprosi/sglasuvane-v-sstava-na-uchtivata-forma-vie-ste-videli-no-vie-ste-vidyan-9/1606): verbs and l-participles in polite address take plural forms. Applying plural imperatives throughout UI is this project's editorial policy.
- IBL BAS advice, [counting persons](https://ibl.bas.bg/ezikovispravki/vaprosi/chislo-na-sshchestvitelnite-broyna-forma-laptopi-ili-laptopove-2/30), independently read in search result full answer: uses ordinary plural for nouns denoting persons. Therefore player/worker labels use e.g. 5 играчи/работници; не 5 играча/работника. Fictional creatures' labels and numeral categories require consistent treatment, not copied Russian declensions.

## Gameplay/source triangulation

Read English `data/texts.en.txt` entries for units, building descriptions, flags and introduction. In particular `intro help build` establishes that worker target is a request, `intro help supply` separates construction/operation staffing, `intro help grow` ties Swarm supply to wheat and creation, and `intro help scout` states flags request units, not instant movement. Read `src/hud/GameGUI.h` swarm production ratio API comments and references for order modifications.

Compared exact labels and descriptions from Serbian, Russian, Ukrainian, French and German native catalogs. Worker is Радник/Рабочий/Робітник/Ouvrière/Arbeiter; explorer is Извиђач/Разведчик/Розвідник/Exploratrice/Entdecker. Swarm is Рој/Рой/Рій/Ruche/Stock; inn is Гостионица/Кабак/Їдальня/Auberge/Gasthof. Comparisons establish ambiguity, not Bulgarian authority. Read English online source entries covering authentication, rooms, replays, colony skins, terrain editing, package releases, payment and licences. The representative pilot includes these domains.

## Review accounting

`semantic-pilot-examined.json` contains exactly 60 examined draft entries with source/target SHA256; `semantic-pilot-findings.json` contains proposed concrete replacements. `pilot-keys.json` proposes 40 native/online identifiers for the next targeted pilot. Blank draft entries after first60 have not been examined. Bulk semantic review must record each actually examined entry, current hashes and findings before claiming complete coverage.
