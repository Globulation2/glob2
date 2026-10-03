# Globulation 2 privacy policy

This policy covers the Globulation 2 apps for Android and iOS (`org.globulation2.glob2`)
and the official online service at [app.glob2online.com](https://app.glob2online.com/)
that they, the desktop game and the browser game use. The Amazon Appstore edition for
Fire tablets has no online play and its own
[Fire tablet privacy policy](amazon-privacy-policy.md).

The online service is operated by **[PLACEHOLDER: operator's legal name and country]**
("we"). For privacy questions and requests, contact
**[PLACEHOLDER: privacy contact address]**.

Last updated: **[PLACEHOLDER: publication date]**.

## In short

- Single-player games, the campaign, the map editor and LAN games work without an
  account and send nothing to us.
- Opening online play creates an account. It starts as a guest account, without an
  e-mail address or a name; a guest gets a generated name such as `Guest-1234`.
- We keep what online play needs: your account, how you sign in, your matches, ratings,
  room chat, maps you upload, and technical data that protects the service.
- Your display name, ratings, match history, match replays and the maps you publish are
  public.
- The apps contain no advertising, analytics or crash-reporting services, and we do
  not sell your data.
- You can delete your account yourself: in the game, **Settings > Online > Download or
  delete my data** opens your account page,
  [app.glob2online.com/account](https://app.glob2online.com/account).

## On your device

The game stores your settings, saved games, maps, replays and downloaded catalog maps on
your device. When you play online, its settings also hold your sign-in for each online
instance you use: a device credential and a refresh token. A multiplayer game (online or LAN) also
writes a network summary file next to its replay (round trips, delays, reconnects and similar
counters, with no names, accounts or addresses). That file stays on your device; the
game never uploads it.

On Android, the app requests only the Internet permission, which it uses for online and
LAN play. Android backup is turned off for the app. On iOS, the app asks for
local-network access when you look for LAN games; iOS may include the app's data in
your device backups, according to your device settings. Neither app requests access to
your contacts, location, camera or microphone. In the browser game, the same data is
kept in your browser's storage for the site.

You can delete local data through your device's storage settings, by uninstalling the
app, or by clearing the site's data in your browser.

## LAN games

LAN games connect devices on the same local network directly. The devices exchange
network addresses, the player names chosen in the game, game data, in-game text chat
and voice chat from players who use it (the mobile apps do not record voice). Other players can keep a record or replay of a shared
match on their devices. LAN games do not use an account and do not contact us.

## Online play

### What we collect and why

| Data | Why |
| --- | --- |
| **Account:** an account number, display name, guest or registered status, creation time, when you were last online, when you last renamed, and any moderation status (role, mute, ban) | To run your account and show who you are to other players. |
| **Sign-in:** for a guest, a device credential, of which we store only a one-way hash, and the kind of device (desktop, Android, iOS, browser). If you link a sign-in provider: the provider's name, its identifier for you and, when the provider shares it, your e-mail address. A new account takes the display name the provider suggests. If you use a username and password: the username and a password hash (argon2id); we never store the password itself. | To sign you in, keep you signed in, and let moderators find an account by its linked e-mail address. |
| **Sessions:** refresh tokens and web-session cookies (stored only as hashes), sign-in attempts from the game to your browser, with a confirmation code and a cookie that binds the attempt to that browser | To keep you signed in safely and detect stolen sign-ins. |
| **Rooms and chat:** rooms you create or join (name, settings, members, seats), room chat messages, and your round trip to each server region | To run rooms and pick a server close to the players. |
| **Matches:** the match setup, the players and their names at the time, results, ratings and rating history, team statistics and timelines, the match record of every order the players gave (including in-game text chat, but not voice chat), the replay and the verification result | To verify results, compute ratings, and show match history and replays. |
| **Connection quality:** per player in a match, the match server's measurements of round trip, how far the player's game ran behind, disconnects, time offline and order counts. These contain no address. | To show connection quality on the match page and investigate network problems. |
| **Maps:** maps and saves you upload, catalog entries you publish (title, description, versions, previews), likes, and reports you file (reason and details). Map downloads are counted once per day per map, by account, or by IP address when you download without signing in. | To run the map catalog and moderate it. |
| **Technical data:** your IP address, the time and the address of each request, kept in server logs and in rate-limit counters. Logs leave out passwords, tokens, cookies and credentials. | To keep the service secure and working, and to stop abuse. |
| **Moderation records:** actions moderators take on accounts, maps and reports | To keep moderation accountable. |

The game also tells the service its version and platform when it connects, so the
service can check that it can play with others.

Voice chat in online matches (the mobile apps do not record voice) passes through the
match server to the other players live and is not stored. We do not use your data for advertising, and we do not combine it
with data from other sources.

### What other people can see

- **Public:** your display name; your profile page (ratings, rating history, match
  history and statistics; guests have only a minimal profile); leaderboards (registered
  accounts only); match pages, including each player's connection quality; match
  replays and verification results, which contain the names players had in the game
  and in-game text chat; public rooms and their names; maps you publish.
- **Other players in a room or match:** your display name, room chat, and what you
  send during the match.
- **Only you and moderators:** your linked sign-in methods and e-mail address. The raw
  match record is available to the match's players and moderators.

Public leaderboard data is also published on the website
[glob2online.com](https://glob2online.com/).

### How long we keep it

| Data | Kept |
| --- | --- |
| Account, display name, matches, ratings, rating history, replays, catalog maps, map reports, moderation records | Until you delete the account (see below for what deletion keeps) |
| Guest accounts | Deleted after 90 days without use if they never played a match, host no open room and own no catalog map |
| Room chat | 30 days |
| Matchmaking requests, including server-region round trips | 30 days |
| Refresh tokens | 7 days after they are replaced or revoked, otherwise 30 days after they expire |
| Web sessions | 30 days after they expire or are revoked |
| Sign-in attempts | 7 days after they finish; provider sign-in steps 24 hours after they expire |
| Rate-limit counters | One day after their last use |
| Server logs | Overwritten as they rotate (at most 100 MB per service container) |
| Uploaded files that nothing uses any more | 7 days |
| Map download counts | Until the map is deleted |
| Backups | **[PLACEHOLDER: whether backups are taken, where they are stored, and for how long]** |

### Deleting your account

Open **Settings > Online > Download or delete my data** in the game, or go to
[app.glob2online.com/account](https://app.glob2online.com/account) and sign in. Confirm
by typing your display name. Deletion takes effect at once and cannot be undone. It:

- signs you out everywhere, and deletes your sign-in methods (with your e-mail address
  and username), your device credentials, and ends your sessions;
- replaces your name with "Deleted player" in your past matches, in stored match
  setups, in other players' chat in your rooms, in the names of rooms you hosted and in
  moderation records;
- deletes your room chat messages, your catalog maps, likes and uploads, and your
  matchmaking requests;
- removes you from leaderboards and player pages.

We keep, under "Deleted player": the account number and its deletion date; the match
history and rating rows that other players' history and ratings depend on; and the
moderation records about the account. Match records and replays are kept unchanged
because they are the verified record of games other people played too, so they still
contain the name you had in that game and your in-game text chat. Files you uploaded
stay stored only while matches played on them refer to them.

A guest account has no way to sign in on the web. To delete a guest account yourself,
link a sign-in method in **Settings > Online** first, then delete the account on the
account page, or ask us at the contact address. Unused guest accounts are also deleted
automatically, as described above.

There is no self-service download of your data yet. Your account page shows your
account and sign-in methods, and your profile page shows your matches.
**[PLACEHOLDER: decide how to answer requests for a copy of your data, and say so here.]**

### Other choices

You can play without an account, play as a guest without an e-mail address, change
your display name in **Settings > Online** (once every 30 days), unlink a sign-in method
on the account page (a registered account keeps at least one), and sign out on a device.

## Service providers

- **Google Cloud** hosts the online service, its database, uploaded files and logs, in
  the `northamerica-northeast2` region (Toronto, Canada).
- **Sign-in providers.** If you choose to sign in with a provider, you sign in on that
  provider's own page, under its privacy policy, and it tells us its identifier for you
  and, if it shares them, your e-mail address and name. The providers offered are
  listed on the sign-in page: **[PLACEHOLDER: the providers enabled on
  app.glob2online.com, e.g. Google, Microsoft, Apple]**.
- **Your browser.** Signing in, the account page, and links to matches, players and
  maps open in your device's browser, under its own privacy terms. The website
  [glob2online.com](https://glob2online.com/) is a separate static site hosted on
  Firebase Hosting.

The service sends your data to no one else.

## Children

Online play does not ask for your age, and guests do not give an e-mail address.
**[PLACEHOLDER: minimum age for online play, or how accounts of children are handled,
and whether parents can ask for a child's account to be deleted.]**

## Self-hosted instances

The game can connect to online instances that other people run. Their operators, not
us, are responsible for the data those instances hold. The game keeps the sign-in for
each instance separately and never sends one instance's credentials to another.

## Changes

We will update this policy, and the store data declarations, before the apps or the
online service collect anything new. The history of this policy is in the
[Globulation 2 source repository](https://github.com/Globulation2/glob2/commits/master/docs/mobile/privacy-policy.md).
