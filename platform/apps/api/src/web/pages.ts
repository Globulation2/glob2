// Server-rendered sign-in pages. They are deliberately plain: no scripts, no
// external resources, a strict Content-Security-Policy, and no Referer, so the
// attempt id in the URL never leaks to a provider or another site.
import { randomBytes } from 'node:crypto';
import type { FastifyReply } from 'fastify';

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

const STYLE = `
:root{color-scheme:light dark;--bg:#f4f1e8;--fg:#1d2a1f;--muted:#5b665c;--card:#fffdf6;--accent:#2f6b3a;--border:#d6cfbd;--warn:#8a4b0f}
@media (prefers-color-scheme:dark){:root{--bg:#141a15;--fg:#e8eee6;--muted:#9aa79b;--card:#1d251e;--accent:#7cc487;--border:#334036;--warn:#f0b26a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:30rem;margin:0 auto;padding:2rem 1rem}h1{font-size:1.4rem;margin:0 0 1rem}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:1.25rem;margin:0 0 1rem}
.code{font:700 2rem/1.2 ui-monospace,Menlo,Consolas,monospace;letter-spacing:.2em;text-align:center;padding:.75rem;border:2px dashed var(--accent);border-radius:8px;margin:.5rem 0 1rem}
.muted{color:var(--muted);font-size:.9rem}.warn{color:var(--warn)}
a.button,button{display:block;width:100%;padding:.75rem 1rem;margin:.5rem 0;border-radius:8px;border:1px solid var(--border);background:var(--card);color:var(--fg);font:inherit;text-align:center;text-decoration:none;cursor:pointer}
a.button.primary,button.primary{background:var(--accent);border-color:var(--accent);color:var(--bg);font-weight:600}
label{display:block;margin:.5rem 0 .25rem}input{width:100%;padding:.6rem;border-radius:6px;border:1px solid var(--border);background:var(--bg);color:var(--fg);font:inherit}
form.inline{display:flex;gap:.5rem}form.inline button{flex:1}
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
  const nonce = extras.script ? randomBytes(16).toString('base64') : undefined;
  return reply
    .status(status)
    .header('content-type', 'text/html; charset=utf-8')
    .header('cache-control', 'no-store')
    .header('referrer-policy', 'no-referrer')
    .header('x-frame-options', 'DENY')
    .header('x-content-type-options', 'nosniff')
    .header(
      'content-security-policy',
      `default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'${nonce ? `; script-src 'nonce-${nonce}'` : ''}`,
    )
    .send(
      html`<!doctype html>
        <html lang="en">
          <head>
            <meta charset="utf-8" />
            <meta name="viewport" content="width=device-width,initial-scale=1" />
            <title>${title}</title>
            ${extras.head}
            <style>
              ${new Html(STYLE)}
            </style>
          </head>
          <body>
            <main>
              <h1>${title}</h1>
              ${body}
            </main>
            ${
              nonce && extras.script
                ? new Html(`<script nonce="${nonce}">${extras.script}</script>`)
                : ''
            }
          </body>
        </html>`.value,
    );
}
