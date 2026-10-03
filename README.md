# Globulation 2 multiplayer: UX, UI and accessibility review, round 3

Reviewer: round-3 UX agent (reviews and fixes). Date: 2026-10-03, 08:00–12:00 EDT.
Targets: `https://app.glob2online.com` (web app, `/j/` invites, `/signin`, `/play/` browser game: threaded runtime, sim 128-51), `https://glob2online.com` (website, main @ 99e7317), and the native client from master af7432520 (after the M9 YOG removal) on macOS arm64.
Screenshots: `review/shots-3/` (prefixes: `A-`/`B-` two-guest browser journey, `P-` iPhone SE browser game, `W-` web pages, `S-` website, `app-<browser>-<config>-*` and `site-*` crawl captures). Raw data: `review/r3/app-{chromium,webkit,firefox}.json`, `review/r3/site-*.json` (axe + targets + overflow + landmarks), scripts `review/r3/crawl.js`, `review/r3/driver3.js` (drives the canvas game through `glob2Diagnostics.snapshot().controls`, so clicks hit real control keys).

## How I tested

- **Crawl:** 14 app routes and 19 website pages × Chromium (desktop light/dark, iPhone SE light/dark, Pixel 7 dark, iPad, 200 % zoom as 640×400@2x, reduced motion), WebKit (desktop, iPhone SE dark, iPad) and Firefox (desktop light/dark, Pixel 7). axe 4 with wcag2a/aa, 21a/aa, 22aa and best-practice; my own 44 px target scan, sideways-overflow check, landmark and live-region inventory, console errors.
- **Played:** two guests in two Chromium contexts — create room → invite page → Play in browser → auto-seat → Ready → Start → in-game panel → guest leaves through the menu → both results → back to room → host Leave → "Close the room?" → guest's "Room closed". Then a Casual quick match with AI backfill (Warrush joined at 0:45), a 10 s network drop (CDP offline), leave, results, hub Recent matches, then **guest → Sign in → web sign-in page → code → register → hub signed in**. Phone: iPhone SE emulation of the hub, account menu and room.
- **Keyboard:** the web app (skip link, focus to `<main>`, visible focus) via the e2e spec; the game hub's focus ring with Tab, and Escape back to the main menu.
- **Not done:** real phones, a real screen reader (VoiceOver/TalkBack), real network loss, the native client by hand (the native screens are reviewed through the same production code running in the browser build, which shares every screen; native-only screens via `mobile-gallery`/UIPresentation captures), and Ranked with a second human.

## Round-1 and round-2 items: are they really fixed?

| ID | Item | Round 3 | Evidence |
|---|---|---|---|
| B-1 | Browser game froze on web links | **Fixed** | Sign in opened `/signin?attempt=…` as a tab, game kept running (`B-21`, `opened: [{via: tab}]`) |
| G-7 | Network drop invisible | **Fixed** | "Connection lost · Reconnecting… attempt 6 · You have 2:42" card at 3 s, own row "Reconnecting" (`B-14`); recovered after 5 s. Card is still the old navy/gold style, see N-9 |
| W-3 | Match page "Custom map" | **Fixed** | "Casual 1v1 · Switchbacks" |
| H-6 | Raw queue id in Recent matches | **Fixed** | "Casual 1v1 · vs Warrush" (`B-19`) |
| H-4/5/7 | Two Online screens, dead ends | **Fixed** | search screen has Profile, Maps, Back to online; results return to the hub |
| G-4 | Winner's old dialog | **Fixed** | "Victory · Guest-9335 left the match." straight to results (`A-08`) |
| G-2 | Panel units/colours, toast overlap | **Mostly fixed** | toast moved; header still says **Ping** above "Behind 1.2 s · Fair" (`A-06`) → fixed in the native PR |
| H-2/H-3/P-5 | Queue names, casual ratings, phone timer | **Fixed** | no ratings on casual, "0:30", "usual wait about 1 min" (`B-10`) |
| F-1 | Kerning ("Tu torial") | **Fixed** | `A-00` |
| R-4/R-11 | Copy/Invite fallback | **Fixed (code)** | not re-tested on a real phone share sheet |
| R-5 | Seat colour changes silently | **Fixed** | chat notice on recolour |
| R-7 | Host Leave closes room without warning | **Fixed** | "Close the room?" (`A-10`) — but the room disappears behind it, N-10 |
| R-8 | "1 people", seed | **Fixed** | "1 player · 0 AI · 1 open" |
| R-9 | Map picker | **Fixed** | simple picker (not re-tested in depth) |
| R-10 | Chat ready state after match | **Not fixed** → fixed in native PR | chat still "Guest-9335 is ready." with no match separator (`B-07`) |
| G-8 | Pause online | **Fixed** | "Pause game (3 left)" on quick match |
| G-9 | Destructive-first leave dialog | **Fixed** | "Keep playing" is the gold primary (`B-05`) |
| I-2 | Closed-room invite | **Fixed** | "This room has closed" |
| A-2 | "Signed in · Continue" interstitial | **Fixed** | "Signed in · Return to the game; you can close this page." |
| A-3 | "Sign in in the browser" | **Not fixed** → fixed in native PR | "Continue with Sign in in the browser", "Finish signing in in your browser", "Nothing to type here" under "Type it there" (`B-20`, `B-22`) |
| A-4 | No account settings | **Fixed** | `/account` exists |
| M-2/M-3 | Catalog empty state / Play this map | **Fixed** | |
| W-1 | Match page data | **Not fixed** → fixed in web PR | duplicate/uneven ticks "0, 1, 1" and "0, 3, 5, 8", raw keys "need food", "alive 0", an economy chart whose two lines are identical |
| W-2 | 404 status 200 | **Partly** | SPA still 200 (by design); now titled and headed (web PR) |
| W-4 | `401 /api/v1/accounts/me` console error | **Not fixed** | 96 of 96 anonymous page loads; API change, left for the platform owner |
| S-1..S-4 | Website dark mode, nav targets, search label, hero | **Fixed** (website redesign #2/#4) | 0 axe violations on 19 pages × 3 browsers |
| AX-2 | Orange hint contrast | **Fixed** | theme `warning` 140,80,10 on paper = 5.0:1 |
| X-1 | One brand | **Mostly fixed** | web app, website, hub, room, menus and results share the paper/gold look; in-game connection card and panel remain navy |
| X-5 | Ranked gold-primary for guests | **Not fixed** | three gold buttons compete on the hub (Ranked Find match, Create room, Sign in) |
| X-6 | Phone nits | **Partly** | hub Maps button now clipped at 375 px (N-3), room hint truncated (N-4) |
| P-3 | Console noise | **Not fixed** | `StringTable::getString("Plantations, -1") no such key` ×8 per quick match, missing `a1–a3.ogg`, `readlink()` |

## Findings, ranked

### P0 — breaks a first impression or a core journey

**W-10 · Web app home: the website strip covers the header.** On the app's home page (the front door from glob2online.com) the dark "← Website · The game · Learn · News · Downloads" strip sits **on top of** the brand, the main nav (Home/Leaderboard/Matches/Maps) and the Sign in button — on every browser and size (`W-10-home-top`, `app-c-se-app_glob2online_com`). axe flags `.brand` "partially obscured, 22 px". The header was `position: absolute; top: 0` on the home page and the strip added later sits above it in flow. The e2e suite missed it because it builds without `VITE_WEBSITE_URL`. **Fixed** (web PR; new e2e test with the strip on).

### P1 — wrong, confusing or inaccessible in a main flow

- **N-1 · Sign-in hand-off copy contradicts itself.** Button "Continue with Sign in in the browser"; waiting card "Finish signing in in your browser. Your browser asks for this code. Type it there: PVK · JVG. The page opened at app.glob2online.com/signin. **Nothing to type here.**" (`B-20`, `B-22`). **Fixed** (native PR): "Open the sign-in page" (primary, Enter), "Finish signing in on the page that opened in your browser. When it asks for a code, enter this one: … Opened app.glob2online.com/signin. Nothing to type in the game; it signs you in by itself."
- **N-2 · Durations disagree.** The same 1:42 match is "1 min" on the results screen and "2 min" in the hub's Recent matches (`B-18`, `B-19`). **Fixed**: one formatter, "1 min 42 s" under 10 minutes.
- **N-3 · Phone hub: Maps button clipped off the card** at 375 px (`P-01`). **Fixed**: Profile & history and Maps share the row.
- **N-4 · Phone room: the start blocker is ellipsized** — "At least two seats must be taken t…" (`P-03`); the instruction is the one thing the host must read. **Fixed**: it wraps, and now says what to do: "Invite a friend or add an AI to start".
- **W-11 · Match page charts and statistics** (W-1 remainder): duplicate/uneven y ticks, lowercase raw keys, `alive 0`, duplicate rows (units = total units), an "Economy against each player's average" chart whose two lines are the same match. **Fixed** (web PR).
- **W-12 · iPhone Safari draws the map filters at 28 px** (WebKit iPhone SE: four selects 103–217×28). **Fixed**: `appearance: none` + own chevron so `min-height: 44px` applies.

### P2 — polish that players will notice

- **N-5 · Guest told about themselves in the third person**: "Waiting for Guest-9335 to be ready" shown to Guest-9335 (`B-02`). **Fixed**: "Press Ready when you're set".
- **N-6 · Seat hint shown to guests who cannot use it**: "Change Team to rebalance" beside disabled Team menus (`B-02`). **Fixed**: host only.
- **N-7 · Room chat keeps pre-match state**: "Guest-9335 is ready." after the match (R-10). **Fixed**: "Match over. Ready up for another round!" line when the room comes back.
- **N-8 · In-game menu puts Leave match between Options and Pause** (`B-04`), and "Pause Game" is the only Title Case item. **Fixed**: Leave last above the pinned Return; "Pause game".
- **N-9 · Connection panel column says "Ping" above "Behind 1.2 s"** (`A-06`). **Fixed**: "Connection".
- **N-10 · Raw relay region "Region ca-central"** on the quick-match card (`B-10`). **Fixed**: "Region Canada, central".
- **N-11 · Results chart's top value is cut in half** ("1(" at the top-right, `A-08`, `B-18`). **Fixed**.
- **N-12 · Desktop main menu: Play online is a small secondary button** below Tutorial (`A-00`), while phones list it second. **Fixed**: same size, under Custom game (feel change; see PR).
- **N-13 · "Back to Online" vs "Back to online"** on the room-closed dialog. **Fixed**.
- **W-13 · Missing match/player/map shows no H1** (axe `page-has-heading-one`) and the SPA 404 is titled with the instance name only. **Fixed**: "Match not found", "Page not found · …".
- **W-14 · Footer and website-strip links are 32–36 px on phones**; the website's top strip 36 px; Pagefind's Clear 40 px wide. **Fixed** (web and website PRs).
- **N-14 · Host's "Close the room?" and the guest's "Room closed" replace the room with the bare colony** (`A-10`, `B-08`): the confirmation loses its context. Not fixed: these are `MessageScreen`s pushed on the stack, which paint their own background; showing them over the room needs a hosted dialog. Recommend porting both to `Glob2UI::Dialog` over the room.
- **N-15 · Connection-lost card and in-game player panel keep the old navy/gold box** while menus and results moved to paper (`B-14`). Not fixed (gameplay HUD palette; a deliberate exception in ui-framework.md, but it now reads as a different product). Design decision for the user.
- **N-16 · Hub: three gold primaries** (Ranked Find match, Create room, Sign in) and Ranked gold for guests who "aren't ranked" (X-5). Not fixed: needs a decision on which single action the hub leads with (I'd lead with Casual for guests, Ranked for signed-in players).
- **N-17 · Results team rows have no outcome marker or "you"** (`A-08`): Victory/Defeat is only in the title. Not fixed.
- **N-18 · Phone room seat rows**: "Guest-5097 · gu…" truncated while "Anyone with the invite can take it" wraps over four lines beside Add AI (`P-03`). Partly addressed (map summary no longer wraps its chevron); the seat row needs a two-line layout on narrow phones.

### P3 — small things

- `?join=CODE` stays in the browser URL after joining; a reload after the room closed tries the dead code again.
- Invite page: the room name is a `<p>`, not a heading; an empty `status` region; "Play in your browser" duplicates the primary button.
- Server-rendered sign-in: the "Code accepted. Choose how to sign in to the game:" sentence sits alone in a card.
- Home page "This instance · format 128, network 51" is admin jargon on the public front page.
- Website copy says "Browser early access · Mouse & keyboard"-era wording in places; the hero note now mentions phones and rooms (website PR).
- Console: `StringTable::getString("<map title>, -1")` on every quick match; missing `data/zik/original/a1–a3.ogg`; `401 accounts/me` (W-4).
- Hub/body helper text is 10–11 px at 1280×800 (comfort scale starts at 1920 wide). Contrast passes (muted 4.5–5.4:1) but size is small for a 13" laptop.

## Accessibility summary

- **Web app:** after the fixes, 0 axe violations (WCAG 2.2 AA + best practice) on 16 pages × desktop/phone × light/dark in the e2e run; landmarks complete (site nav, header, main nav, main, footer navs); focus moves to `<main>` on route change; reduced motion stops the colony; no sideways scroll at 320–430 px or 200 % zoom.
- **Website:** 0 axe violations across 19 pages in Chromium/WebKit/Firefox, dark mode works, 404 returns 404.
- **Game (canvas):** keyboard focus ring works on the hub and Escape routes back; text contrast passes; but the canvas exposes no accessibility tree (AX-1, inherent). The live `#loading-status` and `#open-link` alert are the only screen-reader surfaces. A real screen-reader pass is still needed for the web app and the invite/sign-in pages.

## What needs a person

- Real iPhone/Android: share sheet + copy fallback, the select chevron on iOS Safari, the phone room after N-4/N-18, the sign-in hand-off returning to the game tab.
- VoiceOver/TalkBack on the web app, invite and sign-in pages.
- Desktop feel of Play online moved up the main menu (N-12) and of Leave match moved to the end of the in-game menu (N-8).
- Decisions: N-15 (paper in-game panel/cards?), N-16 (which hub action is primary for guests), N-14 (hosted dialogs over the room).
