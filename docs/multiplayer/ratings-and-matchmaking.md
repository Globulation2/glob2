# Ratings and matchmaking

Quick-match queues group players into matches, and engine-verified results of rated
queue matches update a ladder per queue. Both run in the worker (`platform/apps/worker`), on
the shared match domain in `platform/packages/play`; the
contracts they share with the API, the match starter and clients are listed at the
end. The platform's overall design is in [architecture](architecture.md).

## Ratings

### Model

Ratings are OpenSkill (Weng–Lin) with the Plackett–Luce model, from the npm
`openskill` package. Every option is pinned in `packages/play/src/ratings/scale.ts`, so
a library default change cannot move a ladder:

| Constant | Value | Meaning |
| --- | --- | --- |
| μ₀, σ₀ | 25, 25/3 | Prior for a new player |
| β | 25/6 | Performance variability |
| τ | 25/300 | Added to σ before every game (σ² += τ²), so ratings never freeze |
| z | 3 | Ordinal = μ − 3σ |

Each queue id is its own ladder (`ratings.ladder`). An account has one rating
entity and a rating row per ladder it has played.

### Displayed rating

The displayed rating is the ordinal on a 1500-centred scale:

```
display = 1500 + K · (ordinal − (μ₀ − 3 · σ_P))      K = 400 / (ln 10 · √2 · β) ≈ 29.48
```

- **K** makes display differences read like Elo. Between two settled players the
  Plackett–Luce win probability is 1 / (1 + e^(−Δμ / (√2·β))); equating it with Elo's
  1 / (1 + 10^(−ΔR/400)) gives K. So 400 display points are tenfold odds, as in
  [AI ratings](../ai/ratings.md).
- **σ_P = 5 is the provisional threshold.** A rating with σ > 5 is shown as
  provisional; that takes about 10 games against settled opponents, or about 20
  against new ones. An average player (μ₀) leaving provisional status displays
  1500. A brand-new player displays about 1205 with the provisional flag, and the
  number rises as σ shrinks.
- **Matchmaking uses μ, not the ordinal:** skill = 1500 + K · (μ − μ₀), so a new
  player is matched as 1500 rather than penalised for being unknown.

### Which results change ratings

Ratings change only for **rated queue matches** whose verify-match verdict is
**verified**. Rooms are unrated. The rules live in `ratings/outcome.ts`:

| Situation | Effect |
| --- | --- |
| The verifier reports a winning side | Winner rank 1, loser rank 2 |
| The verifier reports winners on both sides (a shared win) | Draw: no change |
| No winner, and one side's last human left first while an opponent stayed | Abandonment is a loss: the leaving side loses |
| Both sides left within 250 ticks (10 s) of each other | Mutual leave: no change |
| No winner and nobody left (or the match was cut off) | Unresolved: no change |
| Verdict `diverged` or `unverifiable` | No change; the match is flagged for admins |
| A seat holds a deleted account, or one rating entity occupies two seats | No change |

Sides are the `MatchSetup` alliances (`teams[].alliance`). A side wins if any of its
teams won. An AI side never "leaves". In a 2v2, a side leaves only when its last
human leaves; one partner quitting while the other plays on is rated by the result.
Participants marked `abandoned` by match intake count as having left at the final
tick. Intake marks only seats that quit before the end, and a verified winner is
never left `abandoned` ([match intake](rooms-and-matches.md#internal-api-for-relays)).

`diverged` results are not rated. The verifier's replay is authoritative, but a
divergence can come from a broken client as easily as from tampering, so applying
it automatically could punish the wrong player.

### Shared wins are draws

The engine marks a team won when any winning condition says so, and ties count.
When enabled prestige victory reaches its goal, or the sudden-death timer runs
out, surviving teams tied for the most prestige have won, whatever their alliance.
Eliminated colonies cannot set the winning prestige score. The in-game end screen
shows a draw when winning teams belong to different alliances, and a shared win
when they are mutually allied. A sudden-death game in which every surviving team
is on the same prestige at the buzzer (often none at all) therefore marks every
survivor won, which
`result.json` and the verify-match verdict report as such.

The platform reads a win as a win only when one side holds it.
`participantOutcomes` in `ratings/outcome.ts` records each won team as `draw` when
teams of more than one alliance won, for both `match_team_stats` and
`match_participants`; lost and unresolved teams keep their outcome. Draws are not
rated (`rating_note = 'draw'`), count as games but not as wins or losses in profile
statistics, and show as "D" in the web client. Allied teams that won together are
still one winning side. The verifier's raw per-team outcomes stay in the stored
result artifact.

### Empty seats are not participants

A room sends each empty or locked seat as a `closed` seat: its team has no player,
the engine removes its colony at the start and it has lost, exactly like a closed
colony in a custom game. Closed seats get no `match_participants` row and no
`match_team_stats` row, and a player who defeats every real opponent wins.

Matches started before closed seats existed sent empty seats as AI `none`
("Nobody"). Those colonies stayed on the map, alive and idle, and the engine scored
them like any other team: a sudden-death buzzer with everyone on zero prestige
reported them as won. The platform still ignores such teams, and closed ones
(`isEmptySeat` and `contestedTeams` in `ratings/outcome.ts`): they never make a win
shared, a `won` for them is recorded as `unresolved`, and they take no side and are
never rated. A tie among real participants across alliances is still a draw. The
in-game end screen applies the same rule (`WinningConditions` `isGameDrawn`).

### Applying a verdict exactly once

1. The engine agent enqueues its verify-match result. The worker's
   `platform:engine-job-result` handler (`handleEngineJobResult`) runs one
   transaction that:
   - completes the `engine_jobs` row with m0's `applyEngineJobResult`;
   - records the verdict on the match (`verification`, `match_team_stats` with
     the verifier's final team statistics and 512-tick timelines,
     `match_artifacts` for the record, replay and result, participant outcomes);
   - applies ratings.
2. A re-delivered result finds the job already completed and changes nothing.
3. `applyMatchRatings` is idempotent on its own. It locks the match row and only
   proceeds while `matches.rating_status = 'pending'`. It then sets `applied`,
   `unchanged` or `not_rated` in the same transaction. Rating rows are locked in
   entity-id order, so concurrent matches that share players cannot deadlock.
4. Each change is written to:
   - `ratings`: μ, σ, games, wins and last match;
   - `rating_history`: μ/σ and the display value before and after, one row per
     entity per match;
   - `match_participants.rating_before/after`: display values.

   The transaction then sends `NOTIFY match_updates {matchId}`.
5. A scheduler sweep (`applyPendingRatings`, every 30 s) settles any match whose
   verification is known but whose ratings are still pending.

A verify job that fails after its retries (including a last lease that ran out
without a report), or that the stale-job sweep gives up (its accepted report was
lost before the worker applied it, or no agent serves its sim version),
marks the match's verification `failed`; the rating note becomes
`verification_failed` and nothing is rated. The worker logs it at error level. An
administrator re-runs it with `platform matches reverify <match id>` (or `POST
/api/v1/admin/matches/:id/reverify`, audited), which puts the match back to
`pending` and submits a new verify job in one transaction; `platform matches
failed` lists candidates. Re-verification is refused for verified or rated
matches, and while a verify job is queued unless forced. At most one verify job
per match is queued at a time (a partial unique index), so concurrent end-report
retries submit one job.

### AI rating entities

AI entities are keyed by (AI id, sim version). A new sim version gets new entities,
seeded again from [AI ratings](../ai/ratings.md), and never inherits what an older
revision earned. A seed maps the documented Elo to μ on the display scale,
μ = μ₀ + (Elo − 1500) / K, so the AI's matchmaking skill starts at exactly its
documented Elo. Its σ is 6.

The doc's ±15 intervals measure AI against AI; how an AI fares against people is
unknown. A σ of 6 therefore keeps most of a new player's uncertainty and is
provisional (±177 display points per standard deviation). `ratings.seed_source`
records the seed.

| AI | Doc Elo | Seed μ | Seed σ | Initial display |
| --- | ---: | ---: | ---: | ---: |
| Maxima | 1873 | 37.65 | 6 | 1785 |
| Cabino | 1680 | 31.11 | 6 | 1592 |
| Nicowar | 1653 | 30.19 | 6 | 1565 |
| Cortex | 1601 | 28.43 | 6 | 1513 |
| Warrush | 1401 | 21.64 | 6 | 1313 |
| Econo | 1310 | 18.56 | 6 | 1222 |
| Castor | 1280 | 17.54 | 6 | 1192 |
| Numbi | 1204 | 14.96 | 6 | 1116 |

AI entities are then rated from real games like players.

## Queues

Queues are defined in `instance.yaml` (see `platform/instance.example.yaml`).
`resolveQueue()` in `packages/core/src/queueConfig.ts` fills defaults. It also
rejects map pools whose team count does not fit the mode.

| Setting | Default | Meaning |
| --- | --- | --- |
| `mode` | — | `1v1`, or `2v2` solo queue (four players on two sides, each on its own colony) |
| `rated` | — | Rated queues use the ladder named by the queue id. Guests cannot join them. |
| `aiBackfillSeconds` | never | Wait before empty seats are filled with AIs |
| `acceptSeconds` | 10 if rated, else 0 | Accept prompt length for all-human groups. 0 starts at once. AI-backfilled groups never prompt. |
| `declineCooldownSeconds` | 60 | Queue ban after declining or ignoring a prompt |
| `rttPreference` | `{initialMs: 100, perSecondMs: 5, anyRegionAfterSeconds: 30}` | Soft relay preference that widens with wait; never excludes a player |
| `maxRttMs` | off | Opt-in hard cap on a group's worst round trip, for instances with relays near every player. With it set, a player no relay serves within the cap can only be matched through AI backfill. |
| `ratingWindow` | `{initial: 100, perSecond: 5, max: 800}` | Accepted skill difference in display points |
| `aiPool` | all eight rated AIs | AIs that may backfill |
| `mapPool` | fair 128×128 set below | Generator descriptors without a seed |

### Default map pool

The default pool contains every generator registered with a `fairness:` tag, at
128×128 (`width`/`height` 7) with five candidate rolls. These are generators whose
homes are fair by construction ([adding a generator](../map-generators/ADDING_A_GENERATOR.md)):

- **Exact symmetry:** Symmetric Arena, Sierpiński Gardens.
- **Solved fairness:** Even Ground (catchment), Marchland (rope).
- **Repeated wedge:** Amphitheatre, Carousel, City States, Coral, Fjord Continent,
  Lava Shield, Spider Web, Switchbacks, Tidal Flats.
- **Stamped lattice:** Allotments, Bajada, Breachable Highlands, Caravanserai,
  Drumlin Field, Forts, Glacis, Hedgerow Country, Hills, Karst Towers, Locust,
  Old Growth, Plantations, Polder, Rain Shadow, Rice Terraces (with `slant: 0`),
  Ring World.

Emoji (a novelty) and The Gauntlet are left out. The AI rating tournament's
compatible pool also excludes them.

Every entry was generated at 128×128 with the registry's current revisions. Seven of
them refuse four colonies at that size, so the 2v2 pool omits them: Sierpiński
Gardens, Lava Shield, Breachable Highlands, Caravanserai, Glacis, Hills and Rice
Terraces. Rice Terraces needs `slant: 0` even for two colonies.

Revisions are part of each entry. If a generator's revision changes, update the
pool, or engine agents of the new sim version will refuse the old revision.

## Matchmaker

The matchmaker (`apps/worker/src/matchmaking/matchmaker.ts`; grouping, tickets and
proposals in `packages/play/src/matchmaking/`) runs every second as a task of the
worker scheduler. The scheduler runs only on the replica holding the `scheduler`
leader lock, a Postgres advisory lock. All state is in Postgres, so a new leader
continues where the old one stopped.

A dead leader may notice that it lost its session a moment after the new leader
starts. Every step therefore moves rows only from an expected status, inside a
transaction, and the match starter must be idempotent. On top of that the leader
connection uses keepalive and re-checks its lock, and every matchmaker transaction
checks the scheduler's leader epoch first (`assertLease`), so an old leader's
writes fail once a new leader holds the lease.

Each tick:

1. **Resolves accept prompts.**
   - Every human accepted: the proposal moves to `starting`.
   - A decline, or the deadline passing: each decliner or non-responder's ticket
     becomes `declined`, they get a cooldown (`queue_cooldowns`) and
     `queue.proposalEnded {outcome: removed}`.
   - Everyone else waits again with `queue.proposalEnded {outcome: requeued}`.
     Their ticket keeps its original `created_at`, so they keep their queue
     position.
2. **Forms groups** for each queue and sim version from waiting tickets, oldest
   first (`grouping.ts`):
   - **Sim version:** players of different versions are never grouped.
   - **Rating window:** two tickets fit when their skill difference is within the
     wider of their two windows. Each window is
     `min(max, initial + perSecond × waited)`. Every pair in a 2v2 must fit.
   - **Region (soft):** round trips never make a player unmatchable. The group
     plays on the region (relay) that minimises its worst member round trip among
     the relays that exist, even when the only relay is far away. Candidates are
     the regions members probed. When no region is shared by every member with
     probes, the one most members reached wins. Tickets without probes fit any
     region; with no probes at all the region is `null` (any relay).
   - **Region preference:** early in the wait, players are only paired when that
     best relay is within the wider of their tolerances,
     `initialMs + perSecondMs × waited`. After `anyRegionAfterSeconds` (30 s) any
     relay will do, including no shared region at all. So nearby players pair
     first, and everyone else is matched on the best available relay. The opt-in
     `maxRttMs` cap is the only hard exclusion and is off by default.
   - **AI backfill:** an anchor ticket that cannot fill its group, has waited
     `aiBackfillSeconds` and allows AI opponents keeps its compatible AI-allowing
     partners. The remaining seats go to the distinct AIs whose ladder skill (for
     the anchor's sim version) is closest to the humans' mean. A lone player
     always gets their best region, whatever its round trip.
   - **2v2 sides:** sides are split to minimise the difference in summed skill.
     Ties go to the split that spreads humans across sides.
   - **Map:** chosen uniformly from the queue's pool.
3. **Records proposals** (`match_proposals`, `match_proposal_seats`):
   - It claims the tickets (`waiting` → `proposed`). A ticket that left meanwhile
     voids the group.
   - All-human groups in a queue with `acceptSeconds` > 0 (by default, the rated
     queues) become `pending`, with a deadline. Everything else, including every AI-backfilled group, goes
     straight to `starting`.
   - Each human gets `queue.proposal` with the map (generator and size), the
     region and every seat: name, rating, AI id and accept answer, with `you` on
     the receiver's seat (`proposalView.ts`). After each `queue.respond` the API
     sends it again to everyone in the proposal, so the prompt shows who accepted.
4. **Starts** `starting` proposals through `MatchStarter`, in the background:
   a start may wait up to a minute for on-demand map generation, which must not
   hold up proposals, grouping or progress for every queue. Each proposal has at
   most one start in flight (up to 8 at once); a tick waits 250 ms for them and
   counts the rest when they finish.
   - On success, the tickets become `matched` and each human gets
     `queue.matchFound`.
   - After three failed attempts the proposal fails and the players wait again
     (`reason: start_failed`).
5. **Sends progress:** every 5 s, each waiting ticket gets `queue.status` with its
   wait, current rating window and the opponent ratings it admits
   (`ratingRange`, skill ± window), its own displayed rating, the region and round
   trip of its best probe, the AI backfill time and the AI that would take the
   seat now (`backfillAi`), and `typicalWaitSeconds`, the median wait of the
   queue's tickets matched in the last day.

`queue.update {ticketId, allowAiOpponent}` changes "Allow an AI opponent" on a
waiting ticket without losing its position.

**Relay probes.** `GET /api/v1/relays/regions` (public) lists each region with an
available relay and a `probeUrl`: the https form of the public URL of its least
loaded relay. Clients time a request to each (any HTTP response counts) and send
the round trips with `queue.join` and `room.create`. The game client
(`src/online/RelayProbe.cpp`) takes the fastest of three requests per region; a
native request opens a new TCP and TLS connection each time, so it divides by
three (TCP handshake, TLS 1.3 handshake, request), while the browser, which keeps
the connection, reports the fastest request as is.

## Contracts with other workstreams

**Protocol additions** (`packages/protocol`, fixtures regenerated):

- `queue.join` params gain `allowAiOpponent` ("Allow an AI opponent", default true).
- New method `queue.respond {proposalId, accept}`.
- New events `queue.proposal` and `queue.proposalEnded`.
- `QueueInfo` gains `acceptSeconds` and `maps` (generator ids of the pool).
- Later: `queue.update`, the extra `queue.status` and `queue.proposal` fields
  above, and `GET /api/v1/relays/regions` (`RelayRegionList`).

**Schema** (`packages/db/migrations/0002_ratings_matchmaking.sql`):

- `matches`: new columns `rating_status`, `rating_note`, `ratings_applied_at` and
  `proposal_id` (unique).
- `ratings`: new column `seed_source`.
- New tables: `rating_history`, `match_proposals`, `match_proposal_seats` and
  `queue_cooldowns`.
- `queue_tickets`:
  - new statuses `proposed` and `declined`;
  - new columns `allow_ai_opponent` and `proposal_id`;
  - the one-ticket-per-account index now covers `waiting` and `proposed`.

**API realtime handlers** call the ticket operations exported by `@glob2/play`
after their own `AccessPolicy.canQueue` and sim-version checks:

- `joinQueue`: refuses guests in rated queues, active cooldowns and a second
  active ticket.
- `leaveQueue`: leaving during an accept prompt counts as declining.
- `respondToProposal`.

The API forwards `NOTIFY queue_events {accountId, event, data}` to that account's
sockets as the named realtime event. On `NOTIFY match_updates {matchId}` it re-reads
the match and sends `match.updated`.

**`MatchStarter`** receives a `MatchProposal`. Its fields are the proposal
and queue ids, rated and backfilled flags, sim version, region, the map pool entry,
and seats with slot, side, account or AI, rating entity and μ/σ. The starter must:

- create the `matches` row (origin `queue`, `queue_id`, `rated`,
  `proposal_id = proposal.id`) and one `match_participants` row per seat;
- place seat = slot on map team = slot, with `teams[slot].alliance = side`, and
  copy `rating_entity_id` from the seat;
- pick the seed and map: `takeWarmMap(db, queueId, simVersionKey, { entry })`
  from `@glob2/play` returns a pre-generated map of the proposal's pool entry
  (descriptor with seed, map hash, map facts) or undefined, in which case the
  starter submits its own generate-map job; then allocate a relay in the region
  and push `match.start`;
- be idempotent per proposal id, by looking the match up by `matches.proposal_id`;
- throw when the match cannot start.

The production starter is `PlatformMatchStarter`; its MatchSetup comes from
`queueMatchSetup()`, the one builder for queue matches. The test double
`InMemoryMatchStarter` (exported by `@glob2/play/testing`, not by the package
index) writes the rows a real starter would through the same builder, with a
placeholder map.

**Match intake** (M4) sets `match_participants.quit_tick` from the relay's
`RelayMatchEnded` report. It may mark a seat `outcome = 'abandoned'`. Ratings read
both fields.

## Tests

`platform/packages/play/test/ratings.test.ts` and
`platform/apps/worker/test/matchmaker.test.ts` run against a
real Postgres; see [architecture](architecture.md#working-on-the-platform) for the
test database. They cover:

- the published OpenSkill 2v2 vector;
- agreement with an independent Plackett–Luce implementation;
- the display scale;
- AI seeding and per-version entities;
- abandonment and mutual leave;
- idempotent and concurrent application;
- grouping by sim version and widening rating window;
- relays: a single far relay still matches, missing round trips still match, a
  partner on a good shared relay is preferred early and any partner later, and
  the opt-in cap applies only when set;
- backfill timing with a fake clock;
- accept, decline, timeout and leave;
- start failure;
- leader failover after the leader's database session is terminated.
