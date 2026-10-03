// A tiny OpenID Connect issuer for tests: discovery, an authorization endpoint
// that "signs in" whichever user the test chose (query redirect or form_post),
// a token endpoint that checks client authentication, PKCE and redirect URI,
// and RS256 ID tokens.
import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import type { AddressInfo } from 'node:net';
import {
  createHash,
  generateKeyPairSync,
  randomBytes,
  sign,
  verify,
  type KeyObject,
} from 'node:crypto';

export interface MockUser {
  sub: string;
  email?: string;
  name?: string;
}

interface IssuedCode {
  clientId: string;
  redirectUri: string;
  nonce?: string;
  codeChallenge?: string;
  user: MockUser;
}

export interface ClientCheck {
  /** client_secret_post secret, or a verifier for a JWT client secret (Apple). */
  secret?: string;
  verifySecret?: (secret: string) => boolean;
}

export class MockIssuer {
  private readonly server: Server;
  private readonly key = generateKeyPairSync('rsa', { modulusLength: 2048 });
  private readonly codes = new Map<string, IssuedCode>();
  readonly clients = new Map<string, ClientCheck>();
  issuer = '';
  /** The user the next authorization "signs in" as; null simulates the user refusing. */
  nextUser: MockUser | null = { sub: 'user-1', email: 'one@example.org', name: 'Mock One' };
  /** Extra form fields for form_post responses (Apple's `user`). */
  formPostExtra: Record<string, string> = {};
  tokenRequests: URLSearchParams[] = [];

  constructor() {
    this.server = createServer((request, response) => {
      void this.handle(
        request.method ?? 'GET',
        new URL(request.url ?? '/', this.issuer),
        request,
        response,
      );
    });
  }

  async start(): Promise<void> {
    await new Promise<void>((resolve) => this.server.listen(0, '127.0.0.1', resolve));
    this.issuer = `http://127.0.0.1:${(this.server.address() as AddressInfo).port}`;
  }

  stop(): Promise<void> {
    return new Promise((resolve) => this.server.close(() => resolve()));
  }

  private idToken(clientId: string, code: IssuedCode): string {
    const header = { alg: 'RS256', typ: 'JWT', kid: 'mock-1' };
    const now = Math.floor(Date.now() / 1000);
    const claims = {
      iss: this.issuer,
      aud: clientId,
      sub: code.user.sub,
      iat: now,
      exp: now + 300,
      ...(code.nonce ? { nonce: code.nonce } : {}),
      ...(code.user.email ? { email: code.user.email, email_verified: true } : {}),
      ...(code.user.name ? { name: code.user.name } : {}),
    };
    const input = `${Buffer.from(JSON.stringify(header)).toString('base64url')}.${Buffer.from(
      JSON.stringify(claims),
    ).toString('base64url')}`;
    return `${input}.${sign('sha256', Buffer.from(input), this.key.privateKey).toString('base64url')}`;
  }

  private async handle(
    method: string,
    url: URL,
    request: IncomingMessage,
    response: ServerResponse,
  ) {
    const send = (status: number, body: unknown, type = 'application/json') => {
      response.writeHead(status, { 'content-type': type });
      response.end(typeof body === 'string' ? body : JSON.stringify(body));
    };
    if (url.pathname === '/.well-known/openid-configuration') {
      return send(200, {
        issuer: this.issuer,
        authorization_endpoint: `${this.issuer}/authorize`,
        token_endpoint: `${this.issuer}/token`,
        jwks_uri: `${this.issuer}/jwks`,
        response_types_supported: ['code'],
        response_modes_supported: ['query', 'form_post'],
        subject_types_supported: ['public'],
        id_token_signing_alg_values_supported: ['RS256'],
        code_challenge_methods_supported: ['S256'],
        token_endpoint_auth_methods_supported: ['client_secret_post'],
      });
    }
    if (url.pathname === '/jwks') {
      const jwk = this.key.publicKey.export({ format: 'jwk' });
      return send(200, { keys: [{ ...jwk, kid: 'mock-1', alg: 'RS256', use: 'sig' }] });
    }
    if (url.pathname === '/authorize') {
      const p = url.searchParams;
      const redirectUri = p.get('redirect_uri')!;
      const state = p.get('state')!;
      const fields: Record<string, string> = { state };
      if (!this.clients.has(p.get('client_id') ?? '')) {
        Object.assign(fields, { error: 'unauthorized_client' });
      } else if (this.nextUser === null) {
        Object.assign(fields, { error: 'access_denied' });
      } else {
        const code = randomBytes(16).toString('hex');
        this.codes.set(code, {
          clientId: p.get('client_id')!,
          redirectUri,
          ...(p.get('nonce') ? { nonce: p.get('nonce')! } : {}),
          ...(p.get('code_challenge') ? { codeChallenge: p.get('code_challenge')! } : {}),
          user: this.nextUser,
        });
        fields['code'] = code;
        Object.assign(fields, this.formPostExtra);
      }
      if (p.get('response_mode') === 'form_post') {
        // The browser would auto-submit this; tests read the fields from JSON.
        return send(200, { action: redirectUri, fields });
      }
      const target = new URL(redirectUri);
      for (const [k, v] of Object.entries(fields)) target.searchParams.set(k, v);
      response.writeHead(302, { location: target.href });
      return response.end();
    }
    if (url.pathname === '/token' && method === 'POST') {
      const chunks: Buffer[] = [];
      for await (const chunk of request) chunks.push(chunk as Buffer);
      const form = new URLSearchParams(Buffer.concat(chunks).toString());
      this.tokenRequests.push(form);
      const code = this.codes.get(form.get('code') ?? '');
      const clientId = form.get('client_id') ?? '';
      const client = this.clients.get(clientId);
      const secret = form.get('client_secret') ?? '';
      if (!client) return send(401, { error: 'invalid_client' });
      if (client.secret !== undefined && client.secret !== secret)
        return send(401, { error: 'invalid_client' });
      if (client.verifySecret && !client.verifySecret(secret))
        return send(401, { error: 'invalid_client' });
      if (!code || code.clientId !== clientId) return send(400, { error: 'invalid_grant' });
      this.codes.delete(form.get('code')!);
      if (form.get('redirect_uri') !== code.redirectUri)
        return send(400, { error: 'invalid_grant' });
      if (code.codeChallenge) {
        const verifier = form.get('code_verifier') ?? '';
        const challenge = createHash('sha256').update(verifier).digest('base64url');
        if (challenge !== code.codeChallenge)
          return send(400, { error: 'invalid_grant', error_description: 'pkce' });
      }
      return send(200, {
        access_token: randomBytes(16).toString('hex'),
        token_type: 'Bearer',
        expires_in: 300,
        id_token: this.idToken(clientId, code),
      });
    }
    send(404, { error: 'not_found' });
  }
}

/** Verifies an ES256 JWT (Apple client secret) and returns its header and claims. */
export function verifyEs256(token: string, key: KeyObject) {
  const [h, c, s] = token.split('.');
  const ok = verify(
    'sha256',
    Buffer.from(`${h}.${c}`),
    { key, dsaEncoding: 'ieee-p1363' },
    Buffer.from(s!, 'base64url'),
  );
  if (!ok) return undefined;
  return {
    header: JSON.parse(Buffer.from(h!, 'base64url').toString()) as Record<string, unknown>,
    claims: JSON.parse(Buffer.from(c!, 'base64url').toString()) as Record<string, unknown>,
  };
}
