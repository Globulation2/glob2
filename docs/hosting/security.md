# Sign-in providers and key rotation

Configure provider credentials and rotate signing and service keys without interrupting established matches.

## Sign-in providers

Every provider's redirect URI is `<GLOB2_PUBLIC_ORIGIN>/auth/<id>/callback`, where
`<id>` is the provider's `id` in `instance.yaml`. Register exactly that URI. Client
secrets go in `.env` under the name the provider's `clientSecretEnv` gives.
Details of each flow are in [identity and sign-in](../multiplayer/identity.md).

**Google.** In the Google Cloud console, under _APIs & Services → Credentials_,
create an _OAuth client ID_ of type _Web application_. Add the redirect URI
`https://play.example.org/auth/google/callback`. Configure the consent screen with
the `openid`, `email` and `profile` scopes. Then:

```yaml
auth:
  providers:
    - {
        id: google,
        kind: oidc,
        preset: google,
        displayName: Google,
        clientId: 1234-abc.apps.googleusercontent.com,
        clientSecretEnv: GOOGLE_CLIENT_SECRET,
      }
```

From the repository root on a single host, `deploy/configure-signin.py` makes both edits and restarts the
services in one step, taking the secret from standard input so that it stays out
of the command line and the shell history:

```sh
read -rs GOOGLE_SECRET   # paste the client secret, then Enter
printf %s "$GOOGLE_SECRET" | python3 deploy/configure-signin.py /path/to/deployment.env \
    google --client-id 1234-abc.apps.googleusercontent.com --restart
```

It edits only `auth.providers` in the file `GLOB2_INSTANCE_CONFIG` names (other
keys and comments stay), checks the result with PyYAML before writing, and writes
`GOOGLE_CLIENT_SECRET` to the env file. `--remove` takes the provider out again.
With only the `openid`, `email` and `profile` scopes, which Google counts as
non-sensitive, the app needs no scope verification; publish it ("In production")
so that any Google account can sign in, not only listed test users.

**Microsoft.** In the Microsoft Entra admin center, _App registrations → New
registration_. Choose the account types (personal and work accounts: tenant
`common`; personal only: `consumers`; one organization: its tenant id). Add a _Web_
redirect URI `https://play.example.org/auth/microsoft/callback`, then create a
client secret under _Certificates & secrets_:

```yaml
- {
    id: microsoft,
    kind: oidc,
    preset: microsoft,
    tenant: common,
    displayName: Microsoft,
    clientId: 00000000-0000-0000-0000-000000000000,
    clientSecretEnv: MICROSOFT_CLIENT_SECRET,
  }
```

**Apple.** In the Apple Developer portal: an App ID with _Sign in with Apple_, a
_Services ID_ (its identifier is the `clientId`) configured with the domain
`play.example.org` and return URL `https://play.example.org/auth/apple/callback`,
and a _Sign in with Apple_ key (download the `.p8` file, note its key id). Put the
key into `.env` with literal `\n` between lines:

```yaml
- {
    id: apple,
    kind: apple,
    displayName: Apple,
    clientId: org.example.glob2.signin,
    teamId: ABCDE12345,
    keyId: KEY1234567,
    privateKeyEnv: APPLE_SIGNIN_KEY,
  }
```

```dotenv
APPLE_SIGNIN_KEY=-----BEGIN PRIVATE KEY-----\nMIGT...\n-----END PRIVATE KEY-----
```

Apple returns to the callback with a cross-site `form_post`, so the instance must be
served over HTTPS.

**Any OpenID Connect provider** (Keycloak, Authentik, Forgejo, GitLab, Okta…):
create a confidential client with the authorization code flow, the redirect URI
above and the `openid profile email` scopes, then

```yaml
- {
    id: forgejo,
    kind: oidc,
    issuer: https://code.example.org,
    displayName: Example Code,
    clientId: glob2,
    clientSecretEnv: FORGEJO_CLIENT_SECRET,
  }
```

The issuer must serve `/.well-known/openid-configuration`; `platform-api` reaches it
through the `egress` network.

**Without single sign-on**, enable local accounts (argon2id passwords):
`auth.local.enabled: true` and, to let players register themselves,
`allowRegistration: true`.


## Signing keys and rotation

Access tokens and match tickets are EdDSA (Ed25519) JWTs. `platform-api` signs with
one key and publishes every key in `/.well-known/jwks.json`; relays verify tickets
with that JWKS and refetch it when they meet an unknown key id. The keys are
`<kid>.pem` files in the `signing-keys` volume; `init` creates the first one
(`k<yyyymmdd>`) when the volume holds none, and never replaces a key.

Run the rotation procedures below from `deploy/`, where the deployment `.env`
and Compose file live. To rotate (for example yearly, or at once if a key may have leaked):

```sh
# 1. Keep the current key signing while the new one is published.
docker compose exec platform-api ls /var/lib/glob2/keys          # e.g. k20261001.pem
echo JWT_ACTIVE_KID=k20261001 >> .env
docker compose run --rm --no-deps init platform keys generate --dir /var/lib/glob2/keys --kid k20270101
docker compose up -d platform-api
# 2. After the JWKS cache time (5 minutes), sign with the new key.
sed -i 's/^JWT_ACTIVE_KID=.*/JWT_ACTIVE_KID=k20270101/' .env
docker compose up -d platform-api
# 3. Once tokens signed by the old key have expired (access tokens: 10 minutes;
#    tickets: their match's expiry), remove the old key.
docker compose run --rm --no-deps init rm /var/lib/glob2/keys/k20261001.pem
docker compose up -d platform-api
```

After a leak, skip the waiting periods: tokens signed by the removed key stop
verifying at once, which signs every player out and refuses tickets for matches not
yet joined. Refresh tokens are opaque database rows, not JWTs, and are unaffected
by key rotation; revoke them with the admin tools if accounts may be compromised.

The relay key (`relay-secret` volume, `relay.key`, read by the API as
`RELAY_KEYS_FILE` and by relays as `GLOB2_RELAY_KEY_FILE`) authenticates relays on
`/internal`. The API accepts every key in that file and in `RELAY_KEYS`, so rotate
without interruption:

```sh
# 1. Keep accepting the current key while relays switch.
echo "RELAY_KEYS=$(docker compose run --rm --no-deps -T init cat /var/lib/glob2/relay/relay.key)" >> .env
docker compose run --rm --no-deps init rm /var/lib/glob2/relay/relay.key
docker compose up -d --force-recreate init platform-api   # init writes a new relay.key
# 2. Replace the relays (drain them as in "Draining relays"), then drop the old key.
sed -i '/^RELAY_KEYS=/d' .env && docker compose up -d platform-api
```

[Hosting index](README.md) · [Documentation index](../README.md).
