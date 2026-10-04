// Browser sign-in: /signin pages (the handoff target and plain web sign-in),
// provider redirects and callbacks, and local password forms.
//
// A handoff attempt is bound to the first browser that opens its link (a
// cookie whose hash is stored on the attempt). On the game's own network that
// happens at once and the browser goes straight to the provider the player
// picked in the game; elsewhere the player first confirms with one click that
// the code shown matches their game (so a link someone sent them does not sign
// them in to the sender's game). Every later step checks the binding, and
// state-changing forms also check Origin.
import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import { sql } from 'kysely';
import type { Database } from '@glob2/db';
import type { Selectable } from 'kysely';
import { HttpError } from '../errors.ts';
import type { ProviderIdentity } from '../auth/accounts.ts';
import { LOCAL_PROVIDER, hashPassword, normalizeUsername } from '../auth/passwords.ts';
import { ProviderError, randomFlowValues } from '../auth/providers.ts';
import { randomSecret, sha256Hex } from '../auth/secrets.ts';
import {
  authenticate,
  bindingCookieName,
  sameOriginRequest,
  setBindingCookie,
  setSessionCookie,
  type Identity,
} from '../identity.ts';
import { localSignIn } from './auth.ts';
import { html, sendPage, type Html } from '../web/pages.ts';
import { SharedLimit } from '../http/rateLimits.ts';

type Attempt = Selectable<Database['signin_attempts']>;

const FLOW_SECONDS = 15 * 60;
const USERNAME = /^[A-Za-z0-9._-]{3,32}$/;
const MIN_PASSWORD = 10;
const MAX_PASSWORD = 256;

interface Form {
  [key: string]: string | undefined;
}

/**
 * A local-account form as the player left it: which form, what they typed
 * (never the password) and what is wrong, tied to a field so the page can
 * point at it. Errors re-render the whole sign-in page around the form.
 */
interface LocalFormState {
  action: 'signin' | 'register';
  username: string;
  field: 'username' | 'password' | 'form';
  message: string;
}

export async function signinRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
  const { db } = app.services;
  // Per client address and route, across replicas.
  const authLimit = (route: string) => {
    const limit = new SharedLimit(db, `auth:${route}`, identity.limits.authPerMinute, 60_000);
    return {
      preHandler: async (request: FastifyRequest, reply: FastifyReply) => {
        const check = await limit.take(request.ip);
        if (check.allowed) return;
        void reply.header('retry-after', String(check.retryAfterSeconds));
        return errorPage(
          reply,
          'Too many sign-in attempts from here. Wait a minute and try again.',
          429,
        );
      },
    };
  };

  // Never a dead end: every problem page offers a way back.
  const errorPage = (reply: FastifyReply, message: string, status = 400) =>
    sendPage(
      reply,
      'Sign-in problem',
      html`<div class="card" role="alert"><p>${message}</p></div>
        <a class="button primary" href="/signin">Back to sign in</a>
        <a class="button" href="/">Go to ${identity.instanceName}</a>`,
      status,
    );

  const bindingHash = (request: FastifyRequest) => {
    const cookie = request.cookies[bindingCookieName(identity)];
    return cookie ? sha256Hex(cookie) : undefined;
  };

  /** The pending attempt, if this browser is the one bound to it. */
  const boundAttempt = async (request: FastifyRequest, attemptId: string | undefined) => {
    if (!attemptId) return undefined;
    const attempt = await identity.handoff.pending(attemptId);
    const hash = bindingHash(request);
    if (!attempt || !hash || attempt.browser_binding_hash !== hash) return null;
    if (!attempt.code_confirmed_at) return null;
    return attempt;
  };

  /** This browser's binding cookie, or a fresh one to set once the attempt is bound. */
  const browserCookie = (request: FastifyRequest) => {
    const cookie = request.cookies[bindingCookieName(identity)];
    return cookie && /^[A-Za-z0-9_-]{43}$/.test(cookie) ? cookie : randomSecret();
  };

  const platformName: Record<Attempt['client_platform'], string> = {
    desktop: 'on a computer',
    android: 'on an Android device',
    ios: 'on an iPhone or iPad',
    browser: 'in a web browser',
  };

  /** Groups the code the way the game shows it: KXQ · 742. */
  const displayCode = (code: string) =>
    code.length === 6 ? `${code.slice(0, 3)} · ${code.slice(3)}` : code;

  /**
   * For a browser away from the game's network: one click, after comparing
   * the code with the one the game shows.
   */
  const confirmPage = (reply: FastifyReply, attempt: Attempt, status = 200) =>
    sendPage(
      reply,
      'Sign in to Globulation 2',
      html`<div class="card">
          <p>
            Sign in to Globulation 2 ${platformName[attempt.client_platform]}? Your game shows this
            code:
          </p>
          <p class="code"><strong>${displayCode(attempt.confirmation_code)}</strong></p>
          <form method="post" action="/signin/confirm">
            <input type="hidden" name="attempt" value="${attempt.id}" />
            <input type="hidden" name="code" value="${attempt.confirmation_code}" />
            <button class="primary" type="submit">Yes, continue</button>
          </form>
          <p class="muted warn">
            Only continue if you just started signing in from your own game and it shows the same
            code. If someone sent you this link, cancel.
          </p>
        </div>
        <form method="post" action="/signin/cancel">
          <input type="hidden" name="attempt" value="${attempt.id}" /><button>Cancel</button>
        </form>`,
      status,
    );

  /** Where a freshly bound browser goes: straight to the provider picked in the game. */
  const afterBinding = (attempt: Attempt) => {
    const provider = attempt.provider ? identity.providers.get(attempt.provider) : undefined;
    const id = encodeURIComponent(attempt.id);
    return provider
      ? `/auth/${encodeURIComponent(provider.id)}/start?attempt=${id}`
      : `/signin?attempt=${id}`;
  };

  const providerButtons = (attempt: Attempt | undefined) => {
    const query = attempt ? `?attempt=${encodeURIComponent(attempt.id)}` : '';
    const providers = identity.providers.list();
    providers.sort(
      (a, b) => Number(b.id === attempt?.provider) - Number(a.id === attempt?.provider),
    );
    return providers.map(
      (provider, i) =>
        html`<a
          class="button${i === 0 ? ' primary' : ''}"
          href="/auth/${encodeURIComponent(provider.id)}/start${query}"
          >Continue with ${provider.displayName}</a
        >`,
    );
  };

  /** One labelled input, with its problem (if any) tied to it by aria-describedby. */
  const field = (options: {
    id: string;
    label: string;
    name: string;
    type?: string;
    value?: string;
    attrs: Html;
    hint?: string;
    error?: string;
  }) => {
    const described = [
      options.hint ? `${options.id}-hint` : '',
      options.error ? `${options.id}-error` : '',
    ]
      .filter(Boolean)
      .join(' ');
    return html`<label for="${options.id}">${options.label}</label
      ><input
        id="${options.id}"
        name="${options.name}"
        type="${options.type ?? 'text'}"
        ${options.value !== undefined ? html`value="${options.value}"` : ''}
        ${described ? html`aria-describedby="${described}"` : ''}
        ${options.error ? html`aria-invalid="true" autofocus` : ''}
        ${options.attrs}
      />${options.hint ? html`<p class="hint" id="${options.id}-hint">${options.hint}</p>` : ''}${
        options.error
          ? html`<p class="field-error" id="${options.id}-error">${options.error}</p>`
          : ''
      }`;
  };

  /**
   * Local accounts: a Sign in form and, when registration is open, a separate
   * Create account form (so password managers offer to generate a password).
   */
  const localForms = (attempt: Attempt | undefined, state?: LocalFormState): Html | undefined => {
    if (!identity.localAuth.enabled) return undefined;
    const hidden = attempt
      ? html`<input type="hidden" name="attempt" value="${attempt.id}" />`
      : '';
    const mine = (action: LocalFormState['action']) =>
      state?.action === action ? state : undefined;
    const formError = (action: LocalFormState['action']) => {
      const s = mine(action);
      return s?.field === 'form'
        ? html`<p class="field-error form-error" role="alert" id="${action}-error">${s.message}</p>`
        : '';
    };
    const signin = mine('signin');
    const register = mine('register');
    return html`<div class="card">
        <h2>Sign in</h2>
        <form method="post" action="/signin/local" novalidate>
          ${hidden}<input type="hidden" name="action" value="signin" />
          ${formError('signin')}
          ${field({
            id: 'signin-username',
            label: 'Username',
            name: 'username',
            value: signin?.username,
            error: signin?.field === 'username' ? signin.message : undefined,
            attrs: html`autocomplete="username" autocapitalize="none" spellcheck="false" required
            maxlength="32"`,
          })}
          ${field({
            id: 'signin-password',
            label: 'Password',
            name: 'password',
            type: 'password',
            error: signin?.field === 'password' ? signin.message : undefined,
            attrs: html`autocomplete="current-password" required maxlength="${MAX_PASSWORD}"`,
          })}
          <button class="primary" type="submit">Sign in</button>
        </form>
      </div>
      ${
        identity.localAuth.allowRegistration
          ? html`<div class="card">
              <h2>New here? Create an account</h2>
              <form method="post" action="/signin/local" novalidate>
                ${hidden}<input type="hidden" name="action" value="register" />
                ${formError('register')}
                ${field({
                  id: 'register-username',
                  label: 'Choose a username',
                  name: 'username',
                  value: register?.username,
                  hint: '3 to 32 letters, digits, dots, dashes or underscores. Other players see it.',
                  error: register?.field === 'username' ? register.message : undefined,
                  attrs: html`autocomplete="username" autocapitalize="none" spellcheck="false"
                  required minlength="3" maxlength="32" pattern="[A-Za-z0-9._\\-]{3,32}"`,
                })}
                ${field({
                  id: 'register-password',
                  label: 'Choose a password',
                  name: 'password',
                  type: 'password',
                  hint: `At least ${MIN_PASSWORD} characters.`,
                  error: register?.field === 'password' ? register.message : undefined,
                  attrs: html`autocomplete="new-password" required minlength="${MIN_PASSWORD}"
                  maxlength="${MAX_PASSWORD}"`,
                })}
                <button type="submit">Create account</button>
              </form>
            </div>`
          : ''
      }`;
  };

  /** The sign-in page: the handoff confirmation (for the game) or plain web sign-in. */
  const signinPage = (
    reply: FastifyReply,
    view: { attempt?: Attempt; signedInAs?: string; form?: LocalFormState },
    status = 200,
  ) => {
    const { attempt, form } = view;
    if (attempt) {
      // Already signed in on this site: the game can take that account in one click.
      const continueAs =
        view.signedInAs && attempt.mode === 'signin'
          ? html`<div class="card">
              <form method="post" action="/signin/continue">
                <input type="hidden" name="attempt" value="${attempt.id}" />
                <button class="primary" type="submit">Continue as ${view.signedInAs}</button>
              </form>
            </div>`
          : '';
      const providers = providerButtons(attempt);
      return sendPage(
        reply,
        'Sign in to Globulation 2',
        html`${continueAs}
          ${
            providers.length > 0
              ? html`<div class="card">
                  <p>Choose how to sign in to the game:</p>
                  ${providers}
                </div>`
              : ''
          }
          ${localForms(attempt, form)}
          <form method="post" action="/signin/cancel">
            <input type="hidden" name="attempt" value="${attempt.id}" /><button>Cancel</button>
          </form>`,
        status,
      );
    }
    const providers = providerButtons(undefined);
    return sendPage(
      reply,
      'Sign in',
      html`${
          view.signedInAs
            ? html`<div class="card">
                <p>Signed in as <strong>${view.signedInAs}</strong>.</p>
                <a class="button primary" href="/">Continue to ${identity.instanceName}</a>
              </div>`
            : ''
        }
        ${providers.length > 0 ? html`<div class="card">${providers}</div>` : ''}
        ${localForms(undefined, form)}
        <p class="muted">
          No account needed to try it: <a href="/play/">play in your browser</a> as a guest, and
          sign in later to keep your games and rating.
        </p>`,
      status,
    );
  };

  // ------------------------------------------------------------- /signin

  app.get<{ Querystring: { attempt?: string } }>('/signin', async (request, reply) => {
    const attemptId = request.query.attempt;
    if (!attemptId) {
      const caller = await authenticate(identity, request).catch(() => undefined);
      return signinPage(reply, {
        ...(caller ? { signedInAs: caller.account.display_name } : {}),
      });
    }
    const attempt = await identity.handoff.pending(attemptId);
    if (!attempt) {
      return errorPage(
        reply,
        'This sign-in link has expired or was already used. Start signing in again from the game.',
        410,
      );
    }
    const hash = bindingHash(request);
    if (attempt.browser_binding_hash && attempt.browser_binding_hash !== hash) {
      return errorPage(reply, 'This sign-in was already opened in another browser.', 409);
    }
    if (!attempt.code_confirmed_at) {
      if (!identity.handoff.sameNetwork(attempt, request.ip)) return confirmPage(reply, attempt);
      const cookie = browserCookie(request);
      if (!(await identity.handoff.bindBrowser(attempt.id, sha256Hex(cookie)))) {
        return errorPage(reply, 'This sign-in was already opened in another browser.', 409);
      }
      setBindingCookie(identity, reply, cookie);
      return reply.redirect(afterBinding(attempt), 303);
    }
    const caller = await authenticate(identity, request).catch(() => undefined);
    return signinPage(reply, {
      attempt,
      ...(caller ? { signedInAs: caller.account.display_name } : {}),
    });
  });

  app.post<{ Body: Form }>('/signin/confirm', authLimit('confirm'), async (request, reply) => {
    if (!sameOriginRequest(identity, request))
      return errorPage(reply, 'Cross-site request refused.', 403);
    const attemptId = request.body?.attempt;
    const attempt = attemptId ? await identity.handoff.pending(attemptId) : undefined;
    if (!attempt) {
      return errorPage(
        reply,
        'This sign-in link has expired or was already used. Start signing in again from the game.',
        410,
      );
    }
    const typed = (request.body?.code ?? '').slice(0, 64);
    if (!typed.trim()) return confirmPage(reply, attempt, 400);
    const cookie = browserCookie(request);
    const outcome = await identity.handoff.confirmCode(attempt.id, typed, sha256Hex(cookie));
    switch (outcome) {
      case 'confirmed':
        setBindingCookie(identity, reply, cookie);
        return reply.redirect(afterBinding(attempt), 303);
      case 'wrong':
        return confirmPage(reply, attempt, 400);
      case 'locked':
        return errorPage(
          reply,
          'Too many wrong codes, so this sign-in was stopped. Start signing in again from the game.',
          410,
        );
      case 'other_browser':
        return errorPage(reply, 'This sign-in was already opened in another browser.', 409);
      case 'gone':
        return errorPage(
          reply,
          'This sign-in has expired. Start signing in again from the game.',
          410,
        );
    }
  });

  app.post<{ Body: Form }>('/signin/cancel', async (request, reply) => {
    if (!sameOriginRequest(identity, request))
      return errorPage(reply, 'Cross-site request refused.', 403);
    const attempt = await boundAttempt(request, request.body?.attempt);
    if (attempt) await identity.handoff.fail(attempt.id, 'denied');
    return sendPage(
      reply,
      'Sign-in cancelled',
      html`<div class="card"><p>You can close this page.</p></div>`,
    );
  });

  /** The last page of a game sign-in: the game has already been told. */
  const signedInPage = (reply: FastifyReply, displayName: string, linked: boolean) =>
    sendPage(
      reply,
      'Signed in',
      html`<div class="card">
        <p>
          ${linked ? 'Your account is now linked' : 'You are signed in'} as
          <strong>${displayName}</strong>.
        </p>
        <p>Go back to the game: it has signed you in. You can close this tab.</p>
      </div>`,
    );

  // The browser's own web session, handed to the game without another sign-in.
  app.post<{ Body: Form }>('/signin/continue', authLimit('continue'), async (request, reply) => {
    if (!sameOriginRequest(identity, request))
      return errorPage(reply, 'Cross-site request refused.', 403);
    const attempt = await boundAttempt(request, request.body?.attempt);
    if (!attempt || attempt.mode !== 'signin')
      return errorPage(reply, 'This sign-in has expired or belongs to another browser.', 410);
    const caller = await authenticate(identity, request).catch(() => undefined);
    if (!caller || caller.via !== 'cookie') return signinPage(reply, { attempt }, 401);
    await identity.handoff.complete(attempt.id, caller.account.id, false);
    request.log.info(
      { account: caller.account.id, attempt: attempt.id },
      'browser sign-in completed from web session',
    );
    return signedInPage(reply, caller.account.display_name, false);
  });

  // --------------------------------------------------- provider redirects

  app.get<{ Params: { provider: string }; Querystring: { attempt?: string } }>(
    '/auth/:provider/start',
    authLimit('provider-start'),
    async (request, reply) => {
      const provider = identity.providers.get(request.params.provider);
      if (!provider) return errorPage(reply, 'Unknown sign-in provider.', 404);
      let attempt: Attempt | undefined;
      if (request.query.attempt) {
        const bound = await boundAttempt(request, request.query.attempt);
        if (!bound)
          return errorPage(
            reply,
            'This sign-in link has expired or belongs to another browser.',
            410,
          );
        attempt = bound;
        await identity.handoff.setProvider(attempt.id, provider.id);
      }
      const values = randomFlowValues();
      const redirectUri = `${identity.origin}/auth/${provider.id}/callback`;
      let url: URL;
      try {
        url = await provider.authorizationUrl({ ...values, redirectUri });
      } catch (error) {
        request.log.error({ err: error, provider: provider.id }, 'provider unavailable');
        return errorPage(reply, `${provider.displayName} sign-in is unavailable right now.`, 503);
      }
      await db
        .insertInto('auth_flows')
        .values({
          state_hash: sha256Hex(values.state),
          provider: provider.id,
          code_verifier: values.codeVerifier,
          nonce: values.nonce,
          purpose: attempt ? 'handoff' : 'web',
          attempt_id: attempt?.id ?? null,
          browser_binding_hash: attempt ? (bindingHash(request) ?? null) : null,
          expires_at: new Date(Date.now() + FLOW_SECONDS * 1000),
        })
        .execute();
      return reply.header('referrer-policy', 'no-referrer').redirect(url.href, 302);
    },
  );

  const callback = async (
    request: FastifyRequest<{ Params: { provider: string } }>,
    reply: FastifyReply,
    params: URLSearchParams,
  ) => {
    const provider = identity.providers.get(request.params.provider);
    if (!provider) return errorPage(reply, 'Unknown sign-in provider.', 404);
    const state = params.get('state');
    if (!state) return errorPage(reply, 'The sign-in response is missing its state.');
    const flow = await db
      .updateTable('auth_flows')
      .set({ consumed_at: sql<Date>`now()` })
      .where('state_hash', '=', sha256Hex(state))
      .where('provider', '=', provider.id)
      .where('consumed_at', 'is', null)
      .where('expires_at', '>', sql<Date>`now()`)
      .returningAll()
      .executeTakeFirst();
    if (!flow)
      return errorPage(reply, 'This sign-in has expired or was already used. Please start again.');

    let attempt: Attempt | undefined;
    if (flow.purpose === 'handoff') {
      const bound = flow.attempt_id ? await identity.handoff.pending(flow.attempt_id) : undefined;
      if (
        !bound ||
        !flow.browser_binding_hash ||
        bindingHash(request) !== flow.browser_binding_hash
      ) {
        return errorPage(reply, 'This sign-in has expired or belongs to another browser.', 410);
      }
      attempt = bound;
    }

    const url = new URL(`${identity.origin}/auth/${provider.id}/callback`);
    for (const [key, value] of params) if (key !== 'user') url.searchParams.append(key, value);
    let providerIdentity: ProviderIdentity;
    try {
      providerIdentity = await provider.complete(
        { url, user: params.get('user') ?? undefined },
        {
          state,
          nonce: flow.nonce,
          codeVerifier: flow.code_verifier,
          redirectUri: url.origin + url.pathname,
        },
      );
    } catch (error) {
      const reason = error instanceof ProviderError ? error.reason : 'error';
      request.log.warn({ err: error, provider: provider.id }, 'provider sign-in failed');
      if (attempt) await identity.handoff.fail(attempt.id, reason);
      return errorPage(
        reply,
        reason === 'denied'
          ? 'Sign-in was cancelled.'
          : `Signing in with ${provider.displayName} did not work. Please try again.`,
      );
    }
    return finish(request, reply, attempt, providerIdentity, provider.displayName);
  };

  app.get<{ Params: { provider: string } }>(
    '/auth/:provider/callback',
    authLimit('provider-callback'),
    (request, reply) =>
      callback(request, reply, new URL(request.url, identity.origin).searchParams),
  );
  // form_post (Sign in with Apple): a cross-site POST protected by `state`.
  app.post<{ Params: { provider: string }; Body: Form }>(
    '/auth/:provider/callback',
    authLimit('provider-callback'),
    (request, reply) => {
      const params = new URLSearchParams();
      for (const [key, value] of Object.entries(request.body ?? {})) {
        if (typeof value === 'string') params.append(key, value);
      }
      return callback(request, reply, params);
    },
  );

  /** Completes a sign-in once an identity is known. */
  const finish = async (
    request: FastifyRequest,
    reply: FastifyReply,
    attempt: Attempt | undefined,
    providerIdentity: ProviderIdentity,
    providerName: string,
    passwordHash?: string,
  ) => {
    const current =
      attempt?.mode === 'link' && attempt.requesting_account_id
        ? await identity.accounts.get(attempt.requesting_account_id)
        : undefined;
    let outcome;
    try {
      outcome = await identity.accounts.resolveIdentity(providerIdentity, {
        current,
        mode: attempt?.mode ?? 'signin',
        ...(passwordHash ? { passwordHash, createOnly: true } : {}),
      });
    } catch (error) {
      if (error instanceof HttpError) {
        if (attempt) await identity.handoff.fail(attempt.id, 'denied');
        return errorPage(reply, error.body.message, error.statusCode);
      }
      throw error;
    }
    if (outcome.kind === 'conflict') {
      if (!attempt) return errorPage(reply, 'This sign-in is linked to another account.', 409);
      await identity.handoff.recordConflict(attempt.id, outcome.conflict.account.id);
      return sendPage(
        reply,
        'Account already in use',
        html`<div class="card">
          <p>
            This ${providerName} sign-in already belongs to the player
            <strong>${outcome.conflict.account.displayName}</strong>, so it cannot be added to the
            account you are playing as. Accounts are never merged.
          </p>
          <form method="post" action="/signin/choose">
            <input type="hidden" name="attempt" value="${attempt.id}" />
            <button class="primary" name="choice" value="switch">
              Play as ${outcome.conflict.account.displayName} instead
            </button>
            <button name="choice" value="cancel">Keep my current account</button>
          </form>
        </div>`,
        409,
      );
    }
    const { account, linked } = outcome;
    setSessionCookie(identity, reply, await identity.webSessions.create(account.id));
    if (attempt) {
      await identity.handoff.complete(attempt.id, account.id, linked);
      request.log.info(
        { account: account.id, attempt: attempt.id, linked },
        'browser sign-in completed',
      );
      return signedInPage(reply, account.display_name, linked);
    }
    return sendPage(
      reply,
      'Signed in',
      html`<div class="card">
        <p>You are signed in as <strong>${account.display_name}</strong>.</p>
        <a class="button primary" href="/">Continue to ${identity.instanceName}</a>
      </div>`,
    );
  };

  app.post<{ Body: Form }>('/signin/choose', async (request, reply) => {
    if (!sameOriginRequest(identity, request))
      return errorPage(reply, 'Cross-site request refused.', 403);
    const attempt = await boundAttempt(request, request.body?.attempt);
    if (!attempt || !attempt.conflict_account_id) {
      return errorPage(reply, 'This sign-in has expired.', 410);
    }
    if (request.body?.choice === 'switch') {
      const owner = await identity.accounts.get(attempt.conflict_account_id);
      if (!owner || owner.status !== 'active')
        return errorPage(reply, 'That account cannot sign in.', 403);
      setSessionCookie(identity, reply, await identity.webSessions.create(owner.id));
      await identity.handoff.complete(attempt.id, owner.id, false);
      return sendPage(
        reply,
        'Signed in',
        html`<div class="card">
          <p>
            The game will switch to <strong>${owner.display_name}</strong>. You can close this page.
          </p>
        </div>`,
      );
    }
    await identity.handoff.fail(attempt.id, 'conflict');
    return sendPage(
      reply,
      'Nothing changed',
      html`<div class="card">
        <p>Your game keeps its current account. You can close this page.</p>
      </div>`,
    );
  });

  // ------------------------------------------------------ local passwords

  app.post<{ Body: Form }>('/signin/local', authLimit('local'), async (request, reply) => {
    if (!identity.localAuth.enabled)
      return errorPage(reply, 'Local accounts are not enabled.', 404);
    if (!sameOriginRequest(identity, request))
      return errorPage(reply, 'Cross-site request refused.', 403);
    const form = request.body ?? {};
    let attempt: Attempt | undefined;
    if (form.attempt) {
      const bound = await boundAttempt(request, form.attempt);
      if (!bound)
        return errorPage(reply, 'This sign-in has expired or belongs to another browser.', 410);
      attempt = bound;
    }
    const username = (form.username ?? '').trim();
    const password = form.password ?? '';
    const action = form.action === 'register' ? 'register' : 'signin';
    // Problems re-render the page with the form as it was (minus the password).
    const again = (field: LocalFormState['field'], message: string, status: number) =>
      signinPage(
        reply,
        { ...(attempt ? { attempt } : {}), form: { action, username, field, message } },
        status,
      );
    if (username.length === 0) return again('username', 'Enter a username.', 400);
    if (action === 'register') {
      if (!identity.localAuth.allowRegistration)
        return again('form', 'Creating accounts is turned off on this server.', 403);
      if (!USERNAME.test(username)) {
        return again(
          'username',
          username.length < 3 || username.length > 32
            ? `Usernames are 3 to 32 characters long; this one has ${username.length}.`
            : 'Usernames can only use letters, digits, dots, dashes and underscores (no spaces).',
          400,
        );
      }
      if (password.length < MIN_PASSWORD) {
        return again(
          'password',
          password.length === 0
            ? `Choose a password of at least ${MIN_PASSWORD} characters.`
            : `This password is too short: it has ${password.length} characters, and passwords need at least ${MIN_PASSWORD}.`,
          400,
        );
      }
      if (password.length > MAX_PASSWORD)
        return again('password', `Passwords can be at most ${MAX_PASSWORD} characters.`, 400);
      const subject = normalizeUsername(username);
      if (await identity.accounts.localIdentity(subject)) {
        return again(
          'username',
          `The username ${username} is taken. Choose another one, or sign in above if it is yours.`,
          409,
        );
      }
      return finish(
        request,
        reply,
        attempt,
        { provider: LOCAL_PROVIDER, subject, name: username },
        'username',
        await hashPassword(password),
      );
    }
    if (password.length === 0) return again('password', 'Enter your password.', 400);
    if (!USERNAME.test(username) || password.length > MAX_PASSWORD) {
      return again('username', `There is no account called ${username}.`, 401);
    }
    const subject = normalizeUsername(username);
    // Usernames are public (and registration says when one is taken), so
    // telling an unknown username from a wrong password reveals nothing new.
    if (!(await identity.accounts.localIdentity(subject))) {
      return again(
        'username',
        `There is no account called ${username}. Check the spelling${
          identity.localAuth.allowRegistration ? ', or create an account below' : ''
        }.`,
        401,
      );
    }
    try {
      await localSignIn(identity, username, password, request.ip);
    } catch (error) {
      if (error instanceof HttpError) {
        return error.statusCode === 401 && error.body.message === 'Wrong username or password.'
          ? again('password', 'That password is not right for this username. Try again.', 401)
          : again('form', error.body.message, error.statusCode);
      }
      throw error;
    }
    return finish(request, reply, attempt, { provider: LOCAL_PROVIDER, subject }, 'username');
  });
}
