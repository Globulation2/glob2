# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: colony.spec.ts >> real colony video pauses and respects reduced motion
- Location: tests/colony.spec.ts:3:1

# Error details

```
Test timeout of 30000ms exceeded.
```

```
Error: locator.click: Test timeout of 30000ms exceeded.
Call log:
  - waiting for getByRole('button', { name: /Let the globs roam/ })

```

# Page snapshot

```yaml
- generic [active] [ref=e1]:
  - link "Skip to content" [ref=e2]:
    - /url: "#main"
  - banner [ref=e4]:
    - generic [ref=e5]:
      - link "Globulation 2 Online home" [ref=e6]:
        - /url: /
        - generic [aria-hidden] [ref=e8]: Online
      - navigation "Main navigation" [ref=e9]:
        - link "The game" [ref=e10]:
          - /url: /game/
        - link "Learn" [ref=e11]:
          - /url: /learn/
        - link "Community" [ref=e12]:
          - /url: /community/
        - link "Competition" [ref=e13]:
          - /url: /competition/
        - link "News" [ref=e14]:
          - /url: /news/
        - link "Search" [ref=e15]:
          - /url: /search/
      - generic [ref=e16]:
        - link "Sign in" [ref=e17]:
          - /url: https://app.glob2online.com/signin
        - link "Play" [ref=e18]:
          - /url: https://app.glob2online.com/play/
          - text: Play
          - generic [aria-hidden] [ref=e19]: ↗
  - main [ref=e20]:
    - generic [ref=e21]:
      - region [ref=e22]:
        - generic [ref=e25]:
          - paragraph [ref=e26]: Small globs. Big plans.
          - heading "Grow a colony. Find your strategy." [level=1] [ref=e27]: Grow a colony.Find your strategy.
          - paragraph [ref=e28]: A free, open-source real-time strategy game where you assign tasks and your globs get to work.
          - generic [ref=e29]:
            - link "Play in browser" [ref=e30]:
              - /url: https://app.glob2online.com/play/
              - text: Play in browser
              - generic [aria-hidden] [ref=e31]: ↗
            - link "Download the game" [ref=e32]:
              - /url: /downloads/
          - paragraph [ref=e33]:
            - text: Browser early access on computers and phones · Rooms with friends, quick matches, leaderboards and shared maps at
            - link "app.glob2online.com" [ref=e34]:
              - /url: https://app.glob2online.com/
      - figure "Meet your colony. Every glob has a job to do." [ref=e35]:
        - img "Globulation 2 match with turquoise globs, organic buildings, purple flags and resource paths across green terrain" [ref=e36]
      - generic [ref=e38]:
        - paragraph [ref=e39]: A different kind of RTS
        - heading "Think in tasks. Watch a world come alive." [level=2] [ref=e40]: Think in tasks.Watch a world come alive.
        - paragraph [ref=e41]: Choose what needs doing, then let your globs handle the details. Shape an economy, train your colony, and decide where to push next.
      - region "How you play" [ref=e42]:
        - article [ref=e43]:
          - generic [ref=e44]: 01 / GROW
          - heading "Keep your colony thriving." [level=3] [ref=e45]
          - paragraph [ref=e46]: Build inns, gather food, and balance the workers behind every new building.
        - article [ref=e47]:
          - generic [ref=e48]: 02 / PLAN
          - heading "Give direction, not every order." [level=3] [ref=e49]
          - paragraph [ref=e50]: Assign workers and place flags. Your globs find their way to the work.
        - article [ref=e51]:
          - generic [ref=e52]: 03 / ADAPT
          - heading "Make the map your own." [level=3] [ref=e53]
          - paragraph [ref=e54]: Explore, train, and choose where your colony should defend or advance.
      - generic [ref=e55]:
        - generic [ref=e56]:
          - paragraph [ref=e57]: Your first colony starts here
          - heading "New to the world?" [level=2] [ref=e58]
          - paragraph [ref=e59]: Learn the rhythm of food, construction, and training before your first multiplayer match.
          - link "Open the player’s field guide →" [ref=e60]:
            - /url: /learn/
        - generic [ref=e61]:
          - generic [ref=e62]: Play online
          - heading "Climb the ladder. Share your maps." [level=3] [ref=e63]: Climb the ladder.Share your maps.
          - paragraph [ref=e64]: Ranked and casual matches, leaderboards, match replays and a catalog of player-made maps live in the online app.
          - paragraph [ref=e65]:
            - link "Leaderboards ↗" [ref=e66]:
              - /url: https://app.glob2online.com/leaderboard
            - link "Maps ↗" [ref=e67]:
              - /url: https://app.glob2online.com/maps
            - link "Community →" [ref=e68]:
              - /url: /community/
      - generic [ref=e69]:
        - generic [ref=e70]:
          - generic [ref=e71]:
            - paragraph [ref=e72]: From the project
            - heading "News & notes" [level=2] [ref=e73]
          - link "All news →" [ref=e74]:
            - /url: /news/
        - link "Project update Glob2 is growing into the browser Follow development of browser play, shared game logic, and multiplayer cross-play. Read the story →" [ref=e76]:
          - /url: /news/browser-development/
          - generic [ref=e77]: Project update
          - heading "Glob2 is growing into the browser" [level=3] [ref=e78]
          - paragraph [ref=e79]: Follow development of browser play, shared game logic, and multiplayer cross-play.
          - generic [ref=e80]: Read the story →
      - generic [ref=e81]:
        - paragraph [ref=e82]: Your next move
        - heading "Let the globs get to work." [level=2] [ref=e83]
        - link "Play in browser" [ref=e84]:
          - /url: https://app.glob2online.com/play/
          - text: Play in browser
          - generic [aria-hidden] [ref=e85]: ↗
        - link "Get to know the game →" [ref=e86]:
          - /url: /game/
  - contentinfo [ref=e87]:
    - generic [ref=e88]:
      - generic [ref=e89]:
        - link "Globulation 2 Online home" [ref=e90]:
          - /url: /
        - paragraph [ref=e92]: A little less clicking.A lot more strategy.
      - navigation "Explore" [ref=e93]:
        - heading "Explore" [level=2] [ref=e94]
        - list [ref=e95]:
          - listitem [ref=e96]:
            - link "Downloads" [ref=e97]:
              - /url: /downloads/
          - listitem [ref=e98]:
            - link "Player guides" [ref=e99]:
              - /url: /learn/
          - listitem [ref=e100]:
            - link "Our history" [ref=e101]:
              - /url: /history/
          - listitem [ref=e102]:
            - link "Legacy archive" [ref=e103]:
              - /url: /archive/
          - listitem [ref=e104]:
            - link "Privacy" [ref=e105]:
              - /url: /privacy/
      - navigation "Play online" [ref=e106]:
        - heading "Play online" [level=2] [ref=e107]
        - list [ref=e108]:
          - listitem [ref=e109]:
            - link "Play in browser ↗" [ref=e110]:
              - /url: https://app.glob2online.com/play/
          - listitem [ref=e111]:
            - link "Leaderboards ↗" [ref=e112]:
              - /url: https://app.glob2online.com/leaderboard
          - listitem [ref=e113]:
            - link "Matches ↗" [ref=e114]:
              - /url: https://app.glob2online.com/matches
          - listitem [ref=e115]:
            - link "Maps ↗" [ref=e116]:
              - /url: https://app.glob2online.com/maps
          - listitem [ref=e117]:
            - link "Sign in to play online ↗" [ref=e118]:
              - /url: https://app.glob2online.com/signin
      - navigation "Get involved" [ref=e119]:
        - heading "Get involved" [level=2] [ref=e120]
        - list [ref=e121]:
          - listitem [ref=e122]:
            - link "Source & contributions ↗" [ref=e123]:
              - /url: https://github.com/Globulation2/glob2
          - listitem [ref=e124]:
            - link "Community" [ref=e125]:
              - /url: /community/
          - listitem [ref=e126]:
            - link "Credits & sources" [ref=e127]:
              - /url: /credits/
      - paragraph [ref=e128]: "Free software, built by a community. The pictures, fonts and logo come from the Globulation 2 game itself (GPL 3; fonts: DejaVu-based Glob2 Sans and Nunito, SIL OFL)."
```

# Test source

```ts
  1  | import { test, expect } from '@playwright/test';
  2  | 
  3  | test('real colony video pauses and respects reduced motion', async ({ page }) => {
  4  |   await page.emulateMedia({ reducedMotion: 'no-preference' });
  5  |   await page.goto('/');
  6  |   const video = page.locator('.colony-video');
  7  |   await expect.poll(() => video.evaluate((v: HTMLVideoElement) => !v.paused && v.currentTime > 0)).toBe(true);
  8  |   await page.getByRole('button', { name: /Pause the globs/ }).click();
  9  |   expect(await video.evaluate((v: HTMLVideoElement) => v.paused)).toBe(true);
> 10 |   await page.getByRole('button', { name: /Let the globs roam/ }).click();
     |                                                                  ^ Error: locator.click: Test timeout of 30000ms exceeded.
  11 |   await expect.poll(() => video.evaluate((v: HTMLVideoElement) => v.paused)).toBe(false);
  12 |   await page.emulateMedia({ reducedMotion: 'reduce' });
  13 |   await expect.poll(() => video.evaluate((v: HTMLVideoElement) => v.paused)).toBe(true);
  14 |   await expect(video).toBeHidden();
  15 |   await expect(page.getByRole('button', { name: /Pause the globs/ })).toBeHidden();
  16 | });
  17 | 
  18 | test('reduced motion never requests the video and failure retains a poster', async ({ page }) => {
  19 |   const requests: string[] = [];
  20 |   page.on('request', request => { if (request.url().endsWith('.mp4')) requests.push(request.url()); });
  21 |   await page.emulateMedia({ reducedMotion: 'reduce' });
  22 |   await page.goto('/');
  23 |   expect(await page.locator('.colony-video').getAttribute('src')).toBeNull();
  24 |   expect(requests).toEqual([]);
  25 |   await page.route('**/*.mp4', route => route.abort());
  26 |   await page.emulateMedia({ reducedMotion: 'no-preference' });
  27 |   await expect.poll(() => requests.length).toBeGreaterThan(0);
  28 |   await expect(page.getByRole('button', { name: /Pause the globs/ })).toBeHidden();
  29 |   expect(await page.locator('.colony-video').evaluate(v => getComputedStyle(v).opacity)).toBe('0');
  30 | });
  31 | 
```