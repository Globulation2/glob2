// Invite links: https://<instance>/j/<code>. A small server-rendered page
// (so link previews get OpenGraph tags without running scripts) that tries to
// open the installed game with glob2://join?instance=<origin>&code=<code> and
// offers "Play in browser", the web client with ?join=<code>. Unknown and
// expired codes get the same page shape with a clear message.
import type { FastifyInstance } from 'fastify';
import { html } from '../web/pages.ts';
import { sendPage } from '../web/pages.ts';
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

export async function inviteRoutes(app: FastifyInstance, rooms: RoomService): Promise<void> {
  const { config } = app.services;
  const origin = config.publicOrigin;
  const instanceName = config.instance.name;
  const clientUrl = config.instance.web?.browserClientUrl ?? `${origin}/play/`;

  app.get<{ Params: { code: string } }>(
    '/j/:code',
    { config: { rateLimit: { max: 60, timeWindow: 60_000 } } },
    async (request, reply) => {
      const code = request.params.code;
      const room = /^[A-Za-z0-9]{6,16}$/.test(code) ? await rooms.byCode(code) : undefined;
      const live = room && room.status !== 'closed' ? room : undefined;
      const pageUrl = `${origin}/j/${encodeURIComponent(live?.code ?? code)}`;
      const title = live ? `Join ${live.name}` : 'Invite not found';
      const description = live
        ? `${live.host_display_name} invited you to play Globulation 2 on ${instanceName}.`
        : `This invite has expired or does not exist. Ask for a new link.`;
      const appLink = live
        ? appJoinLink(origin, live.code)
        : `glob2://open?instance=${encodeURIComponent(origin)}`;
      const browserLink = browserJoinLink(clientUrl, live?.code);
      const head = html`<meta name="description" content="${description}" />
        <meta property="og:type" content="website" />
        <meta property="og:site_name" content="${instanceName}" />
        <meta property="og:title" content="${title}" />
        <meta property="og:description" content="${description}" />
        <meta property="og:url" content="${pageUrl}" />
        <meta name="twitter:card" content="summary" />
        <meta name="robots" content="noindex" />`;
      const body = live
        ? html`<div class="card">
              <p>${description}</p>
              ${
                live.status === 'in_match'
                  ? html`<p class="muted">
                      They are playing a match right now; you can join the room and wait for the
                      next one.
                    </p>`
                  : ''
              }
              <a class="button primary" id="open-app" href="${appLink}">Open in Globulation 2</a>
              <a class="button" href="${browserLink}">Play in browser</a>
            </div>
            <p class="muted">
              Invite code <strong>${live.code}</strong>. In the game, choose Online, then Join by
              code. If the game does not open, install it or play in the browser.
            </p>`
        : html`<div class="card">
            <p>${description}</p>
            <a class="button" href="${appLink}">Open Globulation 2</a>
            <a class="button" href="${browserLink}">Play in browser</a>
          </div>`;
      // Try the installed game once; if no handler is registered the page stays.
      const script = live
        ? `window.addEventListener('load',function(){setTimeout(function(){window.location.href=${JSON.stringify(appLink)};},300);});`
        : undefined;
      return sendPage(reply, title, body, live ? 200 : 404, {
        head,
        ...(script ? { script } : {}),
      });
    },
  );
}
