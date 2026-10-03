// Browser sign-in: /signin pages (the handoff target and plain web sign-in),
// provider redirects and callbacks, and local password forms.
//
// Handoff attempts are bound to the first browser that opens them (a cookie
// whose hash is stored on the attempt); every later step checks the binding,
// and state-changing forms also check Origin.
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

type Attempt = Selectable<Database['signin_attempts']>;

const FLOW_SECONDS = 15 * 60;
const USERNAME = /^[A-Za-z0-9._-]{3,32}$/;

interface Form {
  [key: string]: string | undefined;
}

export async function signinRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
  const { db } = app.services;
  const authLimit = { rateLimit: { max: identity.limits.authPerMinute, timeWindow: 60_000 } };

  const errorPage = (reply: FastifyReply, message: string, status = 400) =>
    sendPage(reply, 'Sign-in problem', html`<div class="card"><p>${message}</p></div>`, status);

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
    return attempt;
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

  const localForm = (attempt: Attempt | undefined): Html | undefined => {
    if (!identity.localAuth.enabled) return undefined;
    return html`<div class="card">
      <form method="post" action="/signin/local">
        ${attempt ? html`<input type="hidden" name="attempt" value="${attempt.id}" />` : ''}
        <label for="username">Username</label
        ><input
          id="username"
          name="username"
          autocomplete="username"
          required
          minlength="3"
          maxlength="32"
        />
        <label for="password">Password</label
        ><input
          id="password"
          name="password"
          type="password"
          autocomplete="current-password"
          required
          maxlength="256"
        />
        <button class="primary" name="action" value="signin">Sign in</button>
        ${
          identity.localAuth.allowRegistration
            ? html`<button name="action" value="register">Create account</button>
                <p class="muted">New passwords need at least 10 characters.</p>`
            : ''
        }
      </form>
    </div>`;
  };

  // ------------------------------------------------------------- /signin

  app.get<{ Querystring: { attempt?: string } }>('/signin', async (request, reply) => {
    const attemptId = request.query.attempt;
    if (!attemptId) {
      const caller = await authenticate(identity, request).catch(() => undefined);
      return sendPage(
        reply,
        'Sign in',
        html`${
            caller
              ? html`<div class="card">
                  <p>Signed in as <strong>${caller.account.display_name}</strong>.</p>
                  <a class="button primary" href="/">Continue to ${identity.instanceName}</a>
                </div>`
              : ''
          }
          <div class="card">${providerButtons(undefined)}</div>
          ${localForm(undefined)}`,
      );
    }
    const attempt = await identity.handoff.pending(attemptId);
    if (!attempt) {
      return errorPage(
        reply,
        'This sign-in link has expired or was already used. Start signing in again from the game.',
        410,
      );
    }
    let cookie = request.cookies[bindingCookieName(identity)];
    if (!cookie || !/^[A-Za-z0-9_-]{43}$/.test(cookie)) cookie = randomSecret();
    if (!(await identity.handoff.bindBrowser(attempt.id, sha256Hex(cookie)))) {
      return errorPage(reply, 'This sign-in was already opened in another browser.', 409);
    }
    setBindingCookie(identity, reply, cookie);
    return sendPage(
      reply,
      'Sign in to Globulation 2',
      html`<div class="card">
          <p>Check that the game shows this code:</p>
          <div class="code">${attempt.confirmation_code}</div>
          <p class="muted warn">
            Only continue if you started signing in from Globulation 2 yourself just now. Anyone who
            sent you this link could otherwise use your account.
          </p>
          ${providerButtons(attempt)}
        </div>
        ${localForm(attempt)}
        <form method="post" action="/signin/cancel">
          <input type="hidden" name="attempt" value="${attempt.id}" /><button>Cancel</button>
        </form>`,
    );
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

  // --------------------------------------------------- provider redirects

  app.get<{ Params: { provider: string }; Querystring: { attempt?: string } }>(
    '/auth/:provider/start',
    { config: authLimit },
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
    { config: authLimit },
    (request, reply) =>
      callback(request, reply, new URL(request.url, identity.origin).searchParams),
  );
  // form_post (Sign in with Apple): a cross-site POST protected by `state`.
  app.post<{ Params: { provider: string }; Body: Form }>(
    '/auth/:provider/callback',
    { config: authLimit },
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
      return sendPage(
        reply,
        'Signed in',
        html`<div class="card">
          <p>
            ${linked ? 'Your account is now linked' : 'You are signed in'} as
            <strong>${account.display_name}</strong>.
          </p>
          <p>Return to the game; you can close this page.</p>
        </div>`,
      );
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

  app.post<{ Body: Form }>('/signin/local', { config: authLimit }, async (request, reply) => {
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
    const username = form.username ?? '';
    const password = form.password ?? '';
    if (!USERNAME.test(username) || password.length === 0 || password.length > 256) {
      return errorPage(reply, 'Usernames have 3-32 letters, digits, dots, dashes or underscores.');
    }
    const subject = normalizeUsername(username);
    if (form.action === 'register') {
      if (!identity.localAuth.allowRegistration)
        return errorPage(reply, 'Registration is closed.', 403);
      if (password.length < 10) return errorPage(reply, 'Passwords need at least 10 characters.');
      if (await identity.accounts.localIdentity(subject)) {
        return errorPage(reply, 'That username is taken.', 409);
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
    try {
      await localSignIn(identity, username, password);
    } catch (error) {
      if (error instanceof HttpError) return errorPage(reply, error.body.message, error.statusCode);
      throw error;
    }
    return finish(request, reply, attempt, { provider: LOCAL_PROVIDER, subject }, 'username');
  });
}
