# Globulation 2 privacy policy

This policy covers the public website at [glob2.online](https://glob2.online/),
the Globulation 2 apps for Android and iOS (`org.globulation2.glob2`), the desktop
and browser game, and the official online service at
[app.glob2.online](https://app.glob2.online/). The Amazon Appstore edition for
Fire tablets has no online service accounts; its specific practices are in
[Fire tablet edition](#fire-tablet-edition). Website browsing, offline play and
online play have different data practices, described separately below.

The online service is operated by Bradley Arsenault, a sole proprietor, 349 Wheat
Boom Drive, Unit 346, Oakville, Ontario L6H 7X5, Canada ("we"). For privacy
questions and requests, contact **bradley.allen.arsenault@gmail.com**
or write to that address.

Last updated: 26 November 2026. Players aged 13 or over can send us a problem
report from the game's main menu, and on Android and iOS a crash report after
the game closes unexpectedly; see [Problem reports](#problem-reports) and
[Crash reports](#crash-reports).

## Public website browsing

The public website uses optional Google Analytics 4 to understand how people find
and use the site. Analytics stays off until you choose **Allow analytics**. You
can choose **Reject analytics** instead, or change your choice using **Analytics
settings** in the footer. We remember your choice in your browser for six months.
Browsers that signal Do Not Track or Global Privacy Control do not enable
analytics.

When you allow analytics, Google receives page views, the referring website's
origin, limited campaign tags in the link you followed, and clicks on Play,
download and store links. This helps us compare website traffic and interest in
different ways to play. Analytics requests also disclose technical request
information to Google, including your IP address and browser headers. See
[Google's explanation of how it uses information from sites that use its services](https://policies.google.com/technologies/partner-sites).

Analytics uses first-party cookies with a 90-day lifetime to distinguish browsers.
A browser is not necessarily a person: different devices, cleared cookies,
rejected analytics and browser protections affect visitor counts. We do not send
website search terms, account identifiers, login credentials or form contents to
Google Analytics. Page addresses contain canonical paths and the limited campaign
tags; click destinations contain canonical paths only. Referrers exclude paths
and query strings. Advertising features, Google signals
and ad personalization are disabled. This website analytics does not run in the
game or at app.glob2.online.

Changing your choice to reject analytics stops collection and removes the
website's analytics cookies. It does not erase events already sent to Google.
The public website does not use advertising trackers or account cookies. Its
pages and images are delivered by Firebase Hosting. Hosting providers may
process request information, such as IP addresses and browser headers, to deliver
and protect the service.

Search runs in your browser using an index downloaded from the website. Search
terms are not sent to an account service. The competition page downloads a
public ratings snapshot from Google Cloud Storage without sending account
credentials. Published ratings include public usernames, ratings and game counts.

Choosing Play or Log in opens the separate online app at app.glob2.online.
The public website does not store your login credentials. Links to GitHub, the
legacy wiki and other community services take you to those services, where
their own privacy policies apply. Contact us privately using the address above;
avoid posting private information in public repository issues.

## Game and online service summary

- Single-player games, the campaign, the map editor and LAN games work without an
  account and send nothing to us, unless you choose to send us a
  [problem report](#problem-reports) or a [crash report](#crash-reports).
- Opening online play creates an account. It starts as a guest account, without an
  e-mail address or a name; a guest gets a generated name such as `Guest-1234`.
- We keep what online play needs: your account, how you sign in, your matches, ratings,
  room chat, maps you upload, and technical data that protects the service.
- Your display name, ratings, match history, match replays and the maps you publish are
  public.
- We record minimal first-party activity for online accounts, described below. The
  apps contain no advertising or third-party analytics or crash-reporting services;
  nothing tracks you across other apps or sites, and we do not sell your data.
- The game has no minimum age, but chat and other social features are only for
  players aged 13 or over: before LAN or online play the game asks for your age
  range (under 13, or 13 or over) or, on Apple devices, uses Apple's age range
  sharing. We keep only that answer, never a birthday. See [Children](#children).
  Playing online as a guest needs no e-mail address, no real name and no account
  details at all.
- You can download a copy of your data and delete your account yourself: in the
  game, **Settings > Online > Download or delete my data** opens your account page,
  [app.glob2.online/account](https://app.glob2.online/account).

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

The game's settings also hold your age range (under 13, 13 or over, or not yet
answered) and where it came from (your answer, your Apple device, or your online
account). It is used to turn chat and sharing off for younger players, on the
device and, when you play online, on your account.

You can delete local data through your device's storage settings, by uninstalling the
app, or by clearing the site's data in your browser.

## LAN games

LAN games connect devices on the same local network directly. The devices exchange
network addresses, the player names chosen in the game, game data, in-game text chat
and voice chat from players who use it (the mobile apps do not record voice). Text
chat in LAN rooms and matches is off on the device of a player who is under 13 or
has not said their age; the game still plays. Other players can keep a record or replay of a shared
match on their devices. LAN games do not use an account and do not contact us.

## Online play

### What we collect and why

| Data | Why |
| --- | --- |
| **Account:** an account number, display name, guest or registered status, creation time, when you were last online, when you last renamed, and any moderation status (role, mute, ban) | To run your account and show who you are to other players. |
| **Age range:** under 13, 13 or over, or not yet answered; whether it came from your own answer, your device's age signal (Apple's Declared Age Range or Google Play's age signals), or a moderator; and when it was set. Never a birthday. | To turn off chat, sharing, choosing a name and uploading a photo for players under 13 or who have not answered. |
| **Profile photo:** the cropped photo you upload, or a cached Gravatar image, and your photo preference | To display a public photo alongside your name. Uploaded originals and image metadata are not retained. You can choose initials instead. |
| **Sign-in:** for a guest, a device credential, of which we store only a one-way hash, and the kind of device (desktop, Android, iOS, browser). If you link a sign-in provider: the provider's name, its identifier for you and, when the provider shares it, your e-mail address. A new account takes the display name the provider suggests. If you use a username and password: the username and a password hash (argon2id); we never store the password itself. | To sign you in, keep you signed in, and let moderators find an account by its linked e-mail address. |
| **Sessions:** refresh tokens and web-session cookies (stored only as hashes), sign-in attempts from the game to your browser, with a confirmation code and a cookie that binds the attempt to that browser | To keep you signed in safely and detect stolen sign-ins. |
| **Rooms and chat:** rooms you create or join (name, settings, members, seats), room chat messages, and your round trip to each server region | To run rooms and pick a server close to the players. |
| **Matches:** the match setup, the players and their names at the time, results, ratings and rating history, team statistics and timelines, the match record of every order the players gave (including in-game text chat, but not voice chat), the replay and the verification result | To verify results, compute ratings, and show match history and replays. |
| **Connection quality:** per player in a match, the match server's measurements of round trip, how far the player's game ran behind, disconnects, time offline and order counts. These contain no address. | To show connection quality on the match page and investigate network problems. |
| **Maps:** maps and saves you upload, catalog entries you publish (title, description, versions, previews), likes, and reports you file (reason and details). Map downloads are counted once per day per map, by account, or by IP address when you download without signing in. | To run the map catalog and moderate it. |
| **Technical data:** your IP address, the time and the address of each request, kept in server logs and in rate-limit counters. Logs leave out passwords, tokens, cookies and credentials. | To keep the service secure and working, and to stop abuse. |
| **Community music:** uploaded mood tracks and cover art, release titles, descriptions, artist/attribution details, licence, source links, tags, AI disclosure, likes and reports. Published files embed this metadata. | To convert, share and moderate music sets. |
| **Moderation records:** actions moderators take on accounts, maps and reports | To keep moderation accountable. |

The game also tells the service its version and platform when it connects, so the
service can check that it can play with others.

Voice chat in online matches (the mobile apps do not record voice) passes through the
match server to the other players live and is not stored. We do not use your data for advertising. If automatic profile photos are enabled,
we use Gravatar to find a photo associated with your linked e-mail address.

Automatic photos are enabled by default. Our server sends Gravatar a SHA-256 hash
of each linked e-mail address it tries, never the plain address, and caches the
result for 24 hours before checking again on a later request. Photos are served
through our server, so viewing a profile does not send the viewer's browser to
Gravatar. In account settings, upload a replacement or choose **Use initials** to
stop automatic lookups. Replacing or removing a photo deletes our stored copy;
account deletion removes profile photos too.

### What other people can see

- **Public:** your display name and profile photo; your profile page (ratings, rating history, match
  history and statistics; guests have only a minimal profile); leaderboards (registered
  accounts and labelled AI opponents); match pages, including each player's connection quality; match
  replays and verification results, which contain the names players had in the game
  and in-game text chat; public rooms and their names; maps you publish.
- **Other players in a room or match:** your display name, room chat, and what you
  send during the match.
- **Only you and moderators:** your linked sign-in methods and e-mail address. The raw
  match record is available to the match's players and moderators.

Public leaderboard data is also published on the website
[glob2.online](https://glob2.online/).

### How long we keep it

| Data | Kept |
| --- | --- |
| Account, display name, matches, ratings, rating history, replays, catalog maps, map reports, moderation records | Until you delete the account (see below for what deletion keeps) |
| Guest accounts | Deleted after 90 days without use if they never played a match, host no open room and own no catalog map |
| Music source uploads and processing files | Deleted after conversion, cancellation or failure; abandoned/orphan uploads expire within 24 hours |
| Community music releases, likes and reports | Until account deletion; withdrawing or hiding a release stops public access but retains it for administration |
| Room chat | 30 days |
| Matchmaking requests, including server-region round trips | 30 days |
| Refresh tokens | 7 days after they are replaced or revoked, otherwise 30 days after they expire |
| Web sessions | 30 days after they expire or are revoked |
| Sign-in attempts | 7 days after they finish; provider sign-in steps 24 hours after they expire |
| Rate-limit counters | One day after their last use |
| Server logs | Overwritten as they rotate (at most 100 MB per service container) |
| Uploaded files that nothing uses any more | 7 days |
| Map download counts | Until the map is deleted |
| Database backups | Daily backups 7 days, weekly backups about a month (35 days), one backup per month indefinitely (see [Backups](#backups)) |

### Deleting your account

Open **Settings > Online > Download or delete my data** in the game, or go to
[app.glob2.online/account](https://app.glob2.online/account) and sign in. Confirm
by typing your display name. Deletion takes effect at once and cannot be undone. It:

- signs you out everywhere, and deletes your sign-in methods (with your e-mail address
  and username), your device credentials, and ends your sessions;
- replaces your name with "Deleted player" in your past matches, in stored match
  setups, in other players' chat in your rooms, in the names of rooms you hosted and in
  moderation records;
- deletes your room chat messages, your catalog maps, music releases, music reports, likes and uploads, and your
  matchmaking requests;
- deletes your AI Map Studio projects, conversations and generation history, and
  cancels unfinished generation without charging its reserved credit;
- removes you from leaderboards and player pages.

We keep, under "Deleted player": the account number and its deletion date; the match
history and rating rows that other players' history and ratings depend on; and the
moderation records about the account. Credit purchases and ledger entries remain
as financial records; anonymous daily AI call counts remain for service capacity
accounting. Match records and replays are kept unchanged
because they are the verified record of games other people played too, so they still
contain the name you had in that game and your in-game text chat. Previously downloaded music remains on other players’ devices. Files you uploaded
stay stored only while matches played on them refer to them.

#### Backups

We back up the service's database every day to Google Cloud Storage in Toronto
(`northamerica-northeast2`), in a private storage bucket that is never publicly
accessible. Daily backups are deleted after 7 days and weekly backups after about a month
(35 days); one backup from each calendar month is kept indefinitely, so that the
service can be recovered and its match history kept. A backup is a copy of the
database as it was at that moment. **Data of an account deleted after a backup was
taken stays in that backup until the backup itself is deleted, which for monthly
backups means it stays indefinitely.** We do not use backups for anything except
recovering the service.

If we ever restore a backup, the restore re-applies every account deletion recorded
in the newest backup and in the live database, so deleted accounts stay deleted.
The one exception: if the live database were lost as well, a deletion made after
the newest backup (at most about a day earlier) is recorded nowhere and would be
undone. If that ever happens, delete the account again, or ask us to.

#### Guest accounts

A guest account has no way to sign in on the web. To delete a guest account yourself,
link a sign-in method in **Settings > Online** first, then delete the account on the
account page, or ask us at the contact address. Unused guest accounts are also deleted
automatically, as described above.

### Getting a copy of your data

On your account page, **Download my data** saves a file (JSON) with everything the
service stores about your account: your profile, your sign-in methods (without
passwords or keys), your sessions, your matches with your results and connection
quality, your ratings and their history, the rooms you hosted or joined and your
room chat, your matchmaking requests, the maps you published, liked, reported or
uploaded, and moderation actions about your account. You can also ask us for a
copy at the contact address. Replays and maps download from their own pages. A
guest account must first link a sign-in method (see above) to use the account
page; otherwise ask us.

### Other choices

You can play without an account, play as a guest without an e-mail address, change
your display name in **Settings > Online** (once every 30 days), unlink a sign-in method
on the account page (a registered account keeps at least one), and sign out on a device.

## Optional purchases and AI features

When offered, optional website purchases use Stripe checkout. Stripe processes
payment information under its own privacy policy. We retain purchase references,
payment and refund status, amounts and currencies, and credit or entitlement
records for delivery, reconciliation and financial accounting. We do not retain
card details. These financial records remain after account deletion, as described
in [Deleting your account](#deleting-your-account).

AI-assisted features process the prompts, conversations and relevant project
material you submit, together with generated results and validation records.
We use this information to deliver, edit and validate your requested content.
The request inputs needed for generation are sent to OpenAI. Do not submit
sensitive personal information. Saved project content remains until you delete
it or your account; financial ledger records and anonymous operational counts
remain as described above. Deleting a conversation does not delete content you
have separately published in a library. Account deletion does not recall files
other players have already downloaded.

These optional tools are separate from public website browsing and offline
play. The Fire tablet edition does not include these online features.

## Service providers

- **Stripe** processes optional purchases. Its [privacy policy](https://stripe.com/privacy)
  describes how it handles checkout information.
- **OpenAI** processes inputs for optional AI-assisted generation. Its
  [privacy policy](https://openai.com/policies/privacy-policy/) and
  [API data controls](https://platform.openai.com/docs/guides/your-data)
  describe provider data handling. We do not promise that deleting a project
  from our service deletes records a provider retains under its own policies.
- **Google Cloud** hosts the online service, its database, uploaded files and logs, in
  the `northamerica-northeast2` region (Toronto, Canada).
- **Sign-in providers.** If you choose to sign in with a provider, you sign in on that
  provider's own page, under its privacy policy, and it tells us its identifier for you
  and, if it shares them, your e-mail address and name. The providers offered are
  listed on the sign-in page. On app.glob2.online that is **Google**, besides
  a username and password kept by the service itself. Google gives us its
  identifier for you, your e-mail address and your name; Google's
  [privacy policy](https://policies.google.com/privacy) applies to signing in there.
- **Your browser.** Signing in, the account page, and links to matches, players and
  maps open in your device's browser, under its own privacy terms. The website
  [glob2.online](https://glob2.online/) is a separate static site hosted on
  Firebase Hosting.

Apart from the service providers described here, we do not share your data
for advertising or sell it.

## Children

Globulation 2 is a family-friendly game and has no minimum age. Social features
are only for players aged 13 or over. The game and the service are built to collect
as little as possible from anyone, children included:

- Single-player, the campaign, the map editor and LAN games send nothing to us.
  Players under 13, or who have not said their age, cannot send problem or crash
  reports and are never asked to; they can save one to a file and share it with a
  parent's help.
- Online play works as a guest, which needs no e-mail address, no real name, no
  birthday and no account details: the game makes up a name such as `Guest-1234`.
  An account with a username and password needs no e-mail address either. Signing
  in with Google is optional.
- There is no advertising, no browser tracking, no tracking across apps or sites, no
  tracking by advertisers, and we never sell data or use it for marketing.
  Optional purchases and AI tools on the website are described above; they are
  separate from offline gameplay.
- Before LAN or online play, the game asks whether you are under 13 or 13 or over.
  On iPhone, iPad and Apple devices with iOS or iPadOS 26 or later, it first asks
  Apple's Declared Age Range, which a parent can set up through Family Sharing; the
  game learns only whether you are under 13. The Google Play app likewise asks
  Google Play's age signals, where Google Play shares an age range (in some regions,
  and as you or a parent chose); Google Play gives the game an age range and the
  game keeps only whether it is under 13. If you prefer not to answer ("Not
  now"), or decline to share, the game treats you as under 13 until you answer. On
  the website, your account page asks the same question.
- For players under 13, and for players who have not answered, chat is off: they
  cannot send room or in-game chat messages or voice chat, and do not see or hear
  other players'. Online, a match with such a player has chat turned off for
  everyone in it. They can host rooms by invite link but not list public ones. They
  also cannot share maps, AIs, generators, asset sets, buildings or music beyond
  keeping them private, cannot choose a display name (they keep a generated one such as
  `Guest-1234`) and cannot upload a photo. Answering "under 13" makes anything they
  had shared private. Single-player, the campaign, the editor, LAN and online
  matches, including quick matches and rooms, stay available.
- An answer of "under 13" cannot be changed in the game or on the website. When a
  child turns 13, or the answer was a mistake, a parent or guardian can ask us at
  the contact address to change it; on Apple devices, the device's age range
  updates it automatically.
- Players aged 13 or over have room chat and in-game text chat with other players,
  and a registered account chooses its own display name, which is public. Parents
  may want to tell teenagers not to use their real name as a display name or share
  personal details in chat. Moderators can mute, rename and ban accounts.

A parent or guardian can download or delete a child's account on the account page
while signed in to it, or ask us at the contact address. Deletion works exactly as
described in [Deleting your account](#deleting-your-account), including what it
keeps and the note about [backups](#backups). For a guest account, which nobody can
sign in to on the web, tell us the account's display name and roughly when and on
which kind of device it was used; we may ask for more to make sure the account is
the child's before we delete it or send a copy. Uninstalling the app also removes
the guest sign-in from the device, and an unused guest account that never played a
match is deleted automatically after 90 days.

## Online activity and operational reporting

For an online account, we retain at most one activity marker per UTC day when a
successful authenticated request or realtime action occurs. Each marker contains
only the account ID, day and whether the account is a guest or registered.
Anonymous visits, health checks, service credentials and admin dashboard polling
are excluded. There are no browser trackers or acquisition funnels.

Identifiable activity markers are retained for 90 days. Account data exports
include retained markers; deleting an account removes them. Anonymous daily
operational totals are retained for 24 months and cannot be traced back to an
account. Administrators use these totals to understand signups, online activity,
matches, library publication/download counts and studio reliability. Downloads
follow each library's existing counting rules and are not unique visitors.

Administrators also see verified payment and refund amounts, currencies and
payment mode, product credit flows, and provider usage with estimated costs when
usage and monetary rates are available. These records contain no card details,
credentials, private studio prompts or generated source. Financial records
retained for purchase reconciliation remain after account deletion, as
described in [Optional purchases and AI features](#optional-purchases-and-ai-features). Uncertain or unavailable facts are labeled;
we do not infer historical activity from last-seen timestamps.

## Problem reports

**Report a problem** on the main menu lets you tell us what went wrong. Nothing is
sent unless you press **Send**, and only players aged 13 or over can send. A report
holds:

- what you typed under "What happened?" (up to 2,000 characters);
- if you leave them ticked: the game's recent log (its last 200 lines of technical
  messages, such as which screens you opened), your most recently saved game and
  your most recent replay. Saves and replays hold the player names of that game;
- always: the game's version and simulation revision, the platform and operating
  system name, where you got the game (for example the App Store or our website),
  the kind of device (phone, tablet or desktop and a performance tier), the time,
  and a random install ID that the game makes up on first use and that tells us how
  many installations a problem affects.

A report is not linked to your online account, your name or your e-mail address,
and we do not keep your IP address with it; we use the address only briefly, in
memory, to stop floods of reports. Reports go to us at app.glob2.online and to no
one else. The game shows a short report ID that you can quote when you contact us.
If you are offline, the report waits on your device (at most five reports and
10 MB) and is sent from the main menu later.

We use reports only to find and fix problems. Only our administrators can read them.
We delete the attached log, save and replay after 90 days and the rest of the
report after two years. **Save to file** writes the same report to your device
instead, for you to share yourself; it sends nothing. The F-Droid, Linux
distribution, China and Fire tablet editions cannot send reports. To have a report
deleted sooner, send us its ID.

### Crash reports

On Android and iOS, if the game closes unexpectedly, the next time it starts it
asks **Send a crash report?** Nothing is sent unless you press **Send** or
**Always send**; **Don't send** deletes it. **Always send** sends later crash
reports without asking, and **Settings > Privacy** turns that off again. Only
players aged 13 or over are asked; for anyone else the crash stays on the device,
where **Report a problem > Save to file** can include it.

A crash report holds the same "always" details as a problem report (version,
platform, device kind, time and the random install ID) and:

- the technical record of the crash: on Android a minidump (the state of the
  game's threads and a small part of its memory when it crashed, which can by
  chance hold text the game was showing) or the error of the app's Java code;
  on iOS the crash diagnostic iOS itself prepares for the app (MetricKit);
- the game's recent log from the session that crashed, and, if a match was
  running, its map seed and how far it had played.

It never includes what you typed, your saved games or replays, your account,
name or e-mail address, and like problem reports it is not linked to your online
account, and we do not keep your IP address with it. The game captures crashes
itself and sends them only to us at app.glob2.online; no third-party
crash-reporting service is involved. We use crash reports only to find and fix
crashes. We delete the minidump or diagnostic and the log after 90 days, and the
rest (the readable stack trace and the details above) after two years. The
F-Droid, Linux distribution, China and Fire tablet editions do not capture
crashes.

## Your rights

Depending on where you live (for example under Canada's PIPEDA or the EU and UK
GDPR), you can ask for access to your data, its correction or its deletion. The
account page lets you do most of this yourself; otherwise contact us. You can also
complain to your privacy regulator; in Canada that is the
[Office of the Privacy Commissioner of Canada](https://www.priv.gc.ca/).

## Self-hosted instances

The game can connect to online instances that other people run. Their operators, not
us, are responsible for the data those instances hold. The game keeps the sign-in for
each instance separately and never sends one instance's credentials to another.

## Changes

We will update this policy, and the store data declarations, before the apps or the
online service collect anything new. The history of this policy is in the
[Globulation 2 source repository](https://github.com/Globulation2/glob2/commits/master/docs/mobile/privacy-policy.md).

## Fire tablet edition

The Amazon Appstore edition (`org.globulation2.glob2`) has the following
edition-specific practices, carried forward from its policy of 4 October 2026.

### Data on your tablet

The game stores settings, maps, saved games and replays on your tablet. It has no ads,
analytics, or in-app purchases, and does not include a crash-reporting SDK. It
does not request access to contacts, location, camera, or microphone. Android
backup is disabled. You can delete local game data through the tablet's app
storage controls or by uninstalling the game.

### Multiplayer connections

The Fire tablet edition can discover games on a local network and also lets you
enter a host address manually. A manually entered address can lead outside your
local network. The game requests Android's Internet permission for these
connections. When you choose to host or join a game, the host and participants
exchange network addresses, player names chosen in the game, gameplay state,
and any in-game chat messages (chat is off on the tablet of a player who is under
13 or has not said their age). Other participants can retain data from a shared
match on their own devices. The game does not collect this information for the
Globulation 2 project.

### No online play

This edition leaves out online play: it has no online accounts or sign-in, does
not connect to the Globulation 2 online service at app.glob2.online, and does
not open invite links. The game does not send gameplay
or diagnostic data to a project-operated server. The online service, and the
editions that use it, are described in the
[online play section](#online-play).


### Retention and changes

The project does not retain data from this edition on a project-operated server.
Local game data remains until you delete it. We will revise this policy, and the
Appstore privacy questionnaire, before the Fire tablet edition's data practices
change.
