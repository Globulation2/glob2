// Invite links: https://<instance>/j/<code>. A small server-rendered page
// (so link previews get OpenGraph tags without running scripts).
//
// Most people who get a link do not have the game installed, so "Play in
// browser" (the web client with ?join=<code>) is the primary action. "Open in
// the Globulation 2 app" (glob2://join?instance=<origin>&code=<code>) comes
// second, and the page never navigates to it on its own: an unhandled custom
// scheme does nothing in some browsers and shows an error page in others. When
// the button is used, a script watches whether the page lost focus (the app
// took over); if not, it says so and points back to the browser.
//
// Phones are the exception on an instance with verified app links (the
// official domain, `appLinks` in instance.yaml): there, the installed app
// normally opens /j/ links itself, so a phone that still shows this page gets
// the app first. On Android the app link is an intent:// URL whose fallback
// is the browser client, so a missing app still lands somewhere useful.
//
// Unknown and expired codes get the same page shape with a clear message.
import type { FastifyInstance } from 'fastify';
import { DEFAULT_ANDROID_PACKAGE } from '../web/appLinks.ts';
import { ASSETS, html, sendPage } from '../web/pages.ts';
import type { RoomService } from '../play/rooms.ts';

export function appJoinLink(origin: string, code: string): string {
  const url = new URL('glob2://join');
  url.searchParams.set('instance', origin);
  url.searchParams.set('code', code);
  return url.href;
}

export function browserJoinLink(clientUrl: string, code: string | undefined): string {
  const url = new URL(clientUrl);
  if (code) url.searchParams.set('join', code);
  return url.href;
}

/**
 * The same glob2:// link as an Android intent: Chrome opens the app when it is
 * installed and otherwise loads `fallbackUrl` instead of failing silently.
 */
export function androidIntentLink(appLink: string, packageName: string, fallbackUrl: string) {
  const url = new URL(appLink);
  return (
    `intent://${url.host}${url.pathname}${url.search}#Intent;scheme=${url.protocol.slice(0, -1)};` +
    `package=${packageName};S.browser_fallback_url=${encodeURIComponent(fallbackUrl)};end`
  );
}

export type InviteDevice = 'android' | 'ios' | 'other';

export function inviteDevice(userAgent: string | undefined): InviteDevice {
  if (!userAgent) return 'other';
  if (/Android/i.test(userAgent)) return 'android';
  // iPadOS reports a Mac user agent; touch support is not visible server-side.
  if (/iPhone|iPad|iPod/i.test(userAgent)) return 'ios';
  return 'other';
}

/**
 * After "Open in the app": if the page is still in front after a moment, the
 * app did not take over (not installed, or the browser refused): say so.
 */
const OPEN_APP_SCRIPT = `
(function(){
  var link=document.getElementById('open-app');
  var note=document.getElementById('app-fallback');
  var status=document.getElementById('app-status');
  if(!link||!note)return;
  link.addEventListener('click',function(){
    var left=false;
    function away(){left=true;}
    window.addEventListener('blur',away,{once:true});
    document.addEventListener('visibilitychange',function(){if(document.hidden)left=true;},{once:true});
    window.addEventListener('pagehide',away,{once:true});
    if(status)status.textContent='Opening Globulation 2…';
    setTimeout(function(){
      if(status)status.textContent='';
      if(!left&&!document.hidden){note.hidden=false;note.focus();}
    },1600);
  });
})();`;

export async function inviteRoutes(app: FastifyInstance, rooms: RoomService): Promise<void> {
  const { config } = app.services;
  const origin = config.publicOrigin;
  const instanceName = config.instance.name;
  const clientUrl = config.instance.web?.browserClientUrl ?? `${origin}/play/`;
  const appLinks = config.instance.appLinks;
  // Instance names that only repeat the game's name add nothing to "on …".
  const onInstance = /^Globulation 2\b/.test(instanceName) ? '' : ` on ${instanceName}`;

  app.get<{ Params: { code: string } }>(
    '/j/:code',
    { config: { rateLimit: { max: 60, timeWindow: 60_000 } } },
    async (request, reply) => {
      const code = request.params.code;
      const room = /^[A-Za-z0-9]{6,16}$/.test(code) ? await rooms.byCode(code) : undefined;
      const live = room && room.status !== 'closed' ? room : undefined;
      const pageUrl = `${origin}/j/${encodeURIComponent(live?.code ?? code)}`;
      const title = live ? 'You’re invited' : 'Invite not found';
      const description = live
        ? `${live.host_display_name} invited you to their Globulation 2 room${onInstance}.`
        : `This invite has expired or does not exist. Ask for a new link.`;
      const browserLink = browserJoinLink(clientUrl, live?.code);
      const glob2Link = live
        ? appJoinLink(origin, live.code)
        : `glob2://open?instance=${encodeURIComponent(origin)}`;
      const device = inviteDevice(request.headers['user-agent']);
      const android = device === 'android' && appLinks?.android !== undefined;
      const ios = device === 'ios' && appLinks?.ios !== undefined;
      const appFirst = android || ios;
      const appLink = android
        ? androidIntentLink(
            glob2Link,
            appLinks?.android?.packageName ?? DEFAULT_ANDROID_PACKAGE,
            browserLink,
          )
        : glob2Link;
      const head = html`<meta name="description" content="${description}" />
        <meta property="og:type" content="website" />
        <meta property="og:site_name" content="${instanceName}" />
        <meta property="og:title" content="${live ? `Join ${live.name}` : title}" />
        <meta property="og:description" content="${description}" />
        <meta property="og:url" content="${pageUrl}" />
        <meta property="og:image" content="${`${origin}${ASSETS}/og-colony.jpg`}" />
        <meta property="og:image:width" content="1200" />
        <meta property="og:image:height" content="630" />
        <meta
          property="og:image:alt"
          content="A Globulation 2 colony: globs at work around their swarm"
        />
        <meta name="twitter:card" content="summary_large_image" />
        <meta name="robots" content="noindex" />`;
      const browserButton = html`<a
        class="button${appFirst ? '' : ' primary'}"
        id="play-browser"
        href="${browserLink}"
        >Play in browser</a
      >`;
      const appButton = html`<a
        class="button${appFirst ? ' primary' : ''}"
        id="open-app"
        href="${appLink}"
        aria-describedby="app-note"
        >Open in the Globulation 2 app</a
      >`;
      const appNote = html`<p class="muted" id="app-note">
          ${
            appFirst
              ? 'Opens the app if it is installed.'
              : 'Only if you have installed the game on this device.'
          }
          No app? <a href="${browserLink}">Play in your browser</a>; nothing to install.
        </p>
        <p class="sr-only" role="status" id="app-status"></p>
        <div class="warn" id="app-fallback" tabindex="-1" hidden>
          <p><strong>The app didn’t open.</strong></p>
          <p>
            It may not be installed on this device. You can
            <a href="${browserLink}">play in your browser</a> right away, or
            <a href="${`${origin}/`}">get the game</a> and use this link again.
          </p>
        </div>`;
      const body = live
        ? html`<div class="card">
              <p class="eyebrow">Room invite</p>
              <p class="room">${live.name}</p>
              <p>${description}</p>
              ${
                live.status === 'in_match'
                  ? html`<p class="muted">
                      They are playing a match right now; you can join the room and wait for the
                      next one.
                    </p>`
                  : ''
              }
              ${appFirst ? html`${appButton}${browserButton}` : html`${browserButton}${appButton}`}
              ${appNote}
            </div>
            <p class="muted">
              Already in the game? Choose <strong>Play online</strong>, then
              <strong>Join by code</strong>, and enter:
            </p>
            <div class="code">${live.code}</div>`
        : html`<div class="card">
            <p>${description}</p>
            <a class="button primary" href="${browserJoinLink(clientUrl, undefined)}"
              >Play in browser</a
            >
            <a class="button" href="${`${origin}/`}">Go to ${instanceName}</a>
          </div>`;
      return sendPage(reply, title, body, live ? 200 : 404, {
        head,
        ...(live ? { script: OPEN_APP_SCRIPT } : {}),
      });
    },
  );
}
