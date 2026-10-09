// Server-rendered pages (sign-in, invites). They are deliberately plain: no
// scripts beyond one nonce'd inline script where a page needs it, nothing
// from other origins, a strict Content-Security-Policy, and no cross-site Referer, so the
// attempt id in the URL never leaks to a provider or another site.
//
// They share the web app's look (platform/apps/web/src/styles): the game's
// colony behind a paper panel, its wordmark, and the same light and dark
// themes. The few images and fonts they use are served from this API under
// /signin/assets/ (built by platform/apps/web/art/build_art.py).
import { t, resolveLocale, localeFromCookie, locales } from '@glob2/i18n/server';
import { randomBytes } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';

export function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
}

/** Tagged template that escapes interpolations unless they are `Html`. */
export class Html {
  readonly value: string;
  constructor(value: string) {
    this.value = value;
  }
  toString(): string {
    return this.value;
  }
}

export function html(strings: TemplateStringsArray, ...values: unknown[]): Html {
  let out = strings[0] ?? '';
  values.forEach((value, i) => {
    const text = Array.isArray(value)
      ? value.map((v) => (v instanceof Html ? v.value : escapeHtml(String(v)))).join('')
      : value instanceof Html
        ? value.value
        : value === undefined || value === null || value === false
          ? ''
          : escapeHtml(String(value));
    out += text + (strings[i + 1] ?? '');
  });
  return new Html(out);
}

/** Locale belongs to one response; never mutate the browser runtime's global locale. */
const pageLocales = new WeakMap<FastifyReply, string>();
export function setPageLocale(request: FastifyRequest, reply: FastifyReply): void {
  const cookieLocale = localeFromCookie(request.headers.cookie ?? '');
  pageLocales.set(reply, cookieLocale ?? resolveLocale(request.headers['accept-language'] ?? 'en'));
}
export function pageLocale(reply: FastifyReply): string {
  return pageLocales.get(reply) ?? 'en';
}
export function pageText(
  reply: FastifyReply,
  source: string,
  params?: Record<string, string | number>,
): string {
  return t(source, params, pageLocale(reply));
}

/** Translate text nodes while keeping markup and interpolations HTML-escaped. */
export function pageHtml(reply: FastifyReply) {
  return (strings: TemplateStringsArray, ...values: unknown[]): Html => {
    const marker = (index: number) => `\uE000${index}\uE001`;
    let source = strings[0] ?? '';
    values.forEach((_, index) => {
      source += marker(index) + (strings[index + 1] ?? '');
    });
    const translateNode = (_: string, opening: string, node: string) => {
      const trimmed = node.trim();
      if (!trimmed || !/[A-Za-z]/.test(trimmed.replace(/\uE000\d+\uE001/g, '')))
        return opening + node;
      const params: Record<string, string | number> = {};
      const key = trimmed
        .replace(/\uE000(\d+)\uE001/g, (_: string, index: string) => {
          // Slots remain opaque during translation. The final pass inserts their escaped
          // text or trusted Html, allowing translators to move links and emphasis safely.
          params[`p${index}`] = marker(Number(index));
          return `{p${index}}`;
        })
        .replace(/\s+/g, ' ');
      const translated = escapeHtml(pageText(reply, key, params)).replace(
        /&#38;(#\d+|#x[\da-f]+|[a-z][a-z\d]+);/gi,
        '&$1;',
      );
      return (
        opening +
        node.slice(0, node.indexOf(trimmed)) +
        translated +
        node.slice(node.indexOf(trimmed) + trimmed.length)
      );
    };
    const rendered = source
      .split(/(<(?:script|style)\b[^>]*>[\s\S]*?<\/(?:script|style)\s*>)/gi)
      .map((part) =>
        /^<(?:script|style)\b/i.test(part)
          ? part
          : part
              .replace(
                /\b(alt|title|aria-label|placeholder)=("|')([^"']*)\2/g,
                (_: string, name: string, quote: string, text: string) => {
                  if (/\uE000/.test(text)) return `${name}=${quote}${text}${quote}`;
                  return `${name}=${quote}${escapeHtml(pageText(reply, text))}${quote}`;
                },
              )
              .replace(/(^|>)([^<]*)(?=<|$)/g, translateNode),
      )
      .join('');
    return new Html(
      rendered.replace(
        /\uE000(\d+)\uE001/g,
        (_, index: string) => html`${values[Number(index)]}`.value,
      ),
    );
  };
}

/** Public path of a page asset (images and fonts in ./static). */
export const ASSETS = '/signin/assets';

const TYPES: Record<string, string> = {
  webp: 'image/webp',
  png: 'image/png',
  jpg: 'image/jpeg',
  woff2: 'font/woff2',
};
const STATIC_DIR = join(import.meta.dirname, 'static');
const ASSET_NAME = /^[a-z0-9-]+\.(webp|png|jpg|woff2)$/;

/** Serves ./static (read once, cached by browsers for a week). */
export async function pageAssetRoutes(app: FastifyInstance): Promise<void> {
  const cache = new Map<string, Buffer>();
  app.get<{ Params: { file: string } }>(`${ASSETS}/:file`, async (request, reply) => {
    const { file } = request.params;
    const ext = ASSET_NAME.exec(file)?.[1];
    if (!ext) return reply.status(404).send();
    let body = cache.get(file);
    if (!body) {
      try {
        body = readFileSync(join(STATIC_DIR, file));
      } catch {
        return reply.status(404).send();
      }
      cache.set(file, body);
    }
    return reply
      .header('content-type', TYPES[ext] ?? 'application/octet-stream')
      .header('cache-control', 'public, max-age=604800')
      .header('x-content-type-options', 'nosniff')
      .send(body);
  });
}

// Tokens match apps/web/src/styles/tokens.css ("Meadow" light, "Night colony" dark).
const STYLE = `
@font-face{font-family:'Glob2 Sans';src:url(${ASSETS}/glob2-sans.woff2) format('woff2');font-display:swap}
@font-face{font-family:'Nunito Variable';src:url(${ASSETS}/nunito.woff2) format('woff2-variations');font-weight:200 1000;font-display:swap}
:root{color-scheme:light dark;--bg:#f1f1e1;--surface:#fbfbf3;--surface-2:#eaedda;--ink:#1d4530;--ink-2:#4b604c;--line:#ccd5bf;--line-strong:#74866a;--accent:#e3c077;--accent-hover:#ecce8f;--accent-edge:#c69e4c;--accent-ink:#142a1f;--gold-ink:#835607;--focus:#9a5a0c;--warn:#87500a;--warn-bg:#fbf0d9;--warn-line:#e0b878;--danger:#a3362c;--danger-bg:#fbe9e5;--danger-line:#e3aaa1;--two:#e2b85f;--tint:rgba(241,241,225,.0);--shadow:0 2px 0 rgba(15,39,25,.12),0 22px 50px -18px rgba(15,39,25,.55)}
@media (prefers-color-scheme:dark){:root{--bg:#1b1229;--surface:#2b1c42;--surface-2:#34234f;--ink:#f9e8bb;--ink-2:#cfc1a0;--line:#4f3c6b;--line-strong:#8a6db3;--accent-ink:#1e1228;--gold-ink:#f0cf7c;--focus:#ffd678;--warn:#f6c46e;--warn-bg:#45301a;--warn-line:#8a6430;--danger:#ff9488;--danger-bg:#4a1f2e;--danger-line:#8c4250;--tint:rgba(40,22,80,.55);--shadow:0 2px 0 rgba(0,0,0,.3),0 22px 50px -18px rgba(0,0,0,.85)}}
*{box-sizing:border-box}
html{background:#2f6526}
body{margin:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;color:var(--ink);font:16px/1.55 'Nunito Variable',ui-rounded,system-ui,-apple-system,'Segoe UI',sans-serif;background:#2f6526 url(${ASSETS}/colony.webp) center/cover fixed;padding:0 16px}
body::before{content:'';position:fixed;inset:0;background:linear-gradient(180deg,rgba(10,20,12,.35),rgba(10,20,12,.15) 40%,rgba(10,20,12,.45)),linear-gradient(0deg,var(--tint),var(--tint));z-index:-1}
.top{width:100%;max-width:30rem;padding:20px 0 12px}
.brand{display:inline-flex;align-items:center;gap:8px;min-height:44px;padding:4px 14px 4px 6px;background:var(--surface);border-radius:999px;text-decoration:none;color:var(--ink);box-shadow:var(--shadow)}
.brand img{width:34px;height:34px;display:block}
.wordmark{position:relative;display:inline-block;height:22px;aspect-ratio:900/131}
.wordmark::before,.wordmark::after{content:'';position:absolute;inset:0;-webkit-mask:url(${ASSETS}/wordmark-letters.webp) 0 0/100% 100% no-repeat;mask:url(${ASSETS}/wordmark-letters.webp) 0 0/100% 100% no-repeat;background:var(--ink)}
.wordmark::after{-webkit-mask-image:url(${ASSETS}/wordmark-two.webp);mask-image:url(${ASSETS}/wordmark-two.webp);background:var(--two)}
main{width:100%;max-width:30rem;margin:0 0 24px;background:var(--bg);border-radius:24px;padding:24px;box-shadow:var(--shadow)}
h1{font:normal 1.6rem/1.15 'Glob2 Sans','DejaVu Sans',Verdana,sans-serif;letter-spacing:-.01em;margin:0 0 16px;text-wrap:balance}
p{margin:0 0 12px}
a{color:var(--ink);text-decoration-color:var(--accent-edge);text-decoration-thickness:2px;text-underline-offset:3px}
:focus-visible{outline:3px solid var(--focus);outline-offset:2px}
.card{background:var(--surface);border:1px solid var(--line);border-radius:16px;padding:16px;margin:0 0 16px}
.card>:last-child{margin-bottom:0}
.card:empty{display:none}
.code{font:2rem/1.2 'Glob2 Sans','DejaVu Sans',monospace;letter-spacing:.2em;text-align:center;padding:12px;border:2px dashed var(--accent-edge);background:var(--surface-2);border-radius:16px;margin:8px 0 16px}
.muted{color:var(--ink-2);font-size:.9rem}
.warn{color:var(--warn);background:var(--warn-bg);border:1px solid var(--warn-line);border-radius:12px;padding:10px 12px}
.eyebrow{font-size:.78rem;font-weight:800;letter-spacing:.12em;text-transform:uppercase;color:var(--gold-ink);margin:0 0 4px}
.room{font:normal 1.35rem/1.2 'Glob2 Sans','DejaVu Sans',Verdana,sans-serif;margin:0 0 8px;overflow-wrap:anywhere}
a.button,button{display:flex;align-items:center;justify-content:center;width:100%;min-height:48px;padding:10px 18px;margin:8px 0;border-radius:999px;border:1.5px solid var(--line-strong);background:var(--surface);color:var(--ink);font:inherit;font-weight:700;text-align:center;text-decoration:none;cursor:pointer}
a.button:hover,button:hover{background:var(--surface-2)}
a.button.primary,button.primary{background:var(--accent);border-color:var(--accent-edge);color:var(--accent-ink);box-shadow:inset 0 -3px 0 rgba(120,80,10,.18),0 2px 0 var(--accent-edge)}
a.button.primary:hover,button.primary:hover{background:var(--accent-hover)}
label{display:block;margin:12px 0 4px;font-weight:700;font-size:.9rem;color:var(--ink-2)}
h2{font:normal 1.2rem/1.2 'Glob2 Sans','DejaVu Sans',Verdana,sans-serif;margin:0 0 4px}
.hint{margin:4px 0 0;font-size:.85rem;color:var(--ink-2)}
.field-error{margin:6px 0 0;padding:8px 10px;border-radius:10px;font-weight:700;font-size:.9rem;color:var(--danger);background:var(--danger-bg);border:1px solid var(--danger-line)}
.form-error{margin:8px 0}
input[aria-invalid=true]{border-color:var(--danger);border-width:2px}
form>button{margin-top:16px}
input{width:100%;min-height:48px;padding:8px 12px;border-radius:10px;border:1.5px solid var(--line-strong);background:var(--surface);color:var(--ink);font:inherit}
form.inline{display:flex;gap:8px}form.inline button{flex:1}
footer{font-size:.85rem;text-align:center;padding:0 0 24px}
footer p{display:inline-block;margin:0;padding:4px 14px;border-radius:999px;background:rgba(16,30,18,.78);color:#f9e8bb}
footer a{color:#f9e8bb}
@media (max-width:420px){main{padding:18px;border-radius:18px}h1{font-size:1.4rem}}
`;

export interface PageExtras {
  /** Extra <head> content (e.g. OpenGraph tags). */
  head?: Html;
  /** One inline script, allowed by a per-response CSP nonce. */
  script?: string;
}

export function sendPage(
  reply: FastifyReply,
  title: string,
  body: Html,
  status = 200,
  extras: PageExtras = {},
): FastifyReply {
  // Chromium sends Origin: null on native form POSTs with no-referrer.
  // Preserve same-origin Origin for CSRF checks; external referrers stay hidden.
  const locale = pageLocale(reply);
  const direction = locales.find((item) => item.code === locale)?.dir ?? 'ltr';
  title = pageText(reply, title);
  const nonce = extras.script ? randomBytes(16).toString('base64') : undefined;
  return reply
    .status(status)
    .header('content-type', 'text/html; charset=utf-8')
    .header('cache-control', 'no-store')
    .header('referrer-policy', 'same-origin')
    .header('x-frame-options', 'DENY')
    .header('x-content-type-options', 'nosniff')
    .header(
      'content-security-policy',
      `default-src 'none'; style-src 'unsafe-inline'; img-src 'self'; font-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'${nonce ? `; script-src 'nonce-${nonce}'` : ''}`,
    )
    .send(
      html`<!doctype html>
        <html lang="${locale}" dir="${direction}">
          <head>
            <meta charset="utf-8" />
            <meta name="viewport" content="width=device-width,initial-scale=1" />
            <meta name="color-scheme" content="light dark" />
            <meta name="theme-color" content="#2f6526" />
            <title>${title === 'Globulation 2' ? title : `${title} · Globulation 2`}</title>
            <link rel="icon" type="image/png" sizes="32x32" href="${ASSETS}/favicon-32.png" />
            ${extras.head}
            <style>
              ${new Html(STYLE)}
            </style>
          </head>
          <body>
            <header class="top">
              <a class="brand" href="/"
                ><img src="${ASSETS}/glob-64.png" width="34" height="34" alt="" /><span
                  class="wordmark"
                  role="img"
                  aria-label="Globulation 2"
                ></span
              ></a>
            </header>
            <main>
              <h1>${title}</h1>
              ${body}
            </main>
            <footer><p>${pageText(reply, 'Globulation 2 is free software (GPL 3).')}</p></footer>
            ${
              nonce && extras.script
                ? new Html(`<script nonce="${nonce}">${extras.script}</script>`)
                : ''
            }
          </body>
        </html>`.value,
    );
}
