// Verified app links for invites and catalog map launches, from instance config
// `appLinks`: Android App Links (/.well-known/assetlinks.json) and iOS
// universal links (/.well-known/apple-app-site-association). Unconfigured
// instances answer 404, so a phone never associates a self-hosted domain with
// the official apps.
import type { FastifyInstance } from 'fastify';
import { apiError } from '../errors.ts';

export const DEFAULT_ANDROID_PACKAGE = 'org.globulation2.glob2';
/** Paths the apps handle; everything else on the domain stays in the browser. */
export const APP_LINK_PATHS = ['/j/*', '/play/*'] as const;

export async function appLinkRoutes(app: FastifyInstance): Promise<void> {
  const links = app.services.config.instance.appLinks;
  const headers = { 'content-type': 'application/json', 'cache-control': 'public, max-age=3600' };

  app.get('/.well-known/assetlinks.json', async (_request, reply) => {
    const android = links?.android;
    if (!android) throw apiError('not_found', 'No Android app is associated with this instance.');
    return reply.headers(headers).send([
      {
        relation: ['delegate_permission/common.handle_all_urls'],
        target: {
          namespace: 'android_app',
          package_name: android.packageName ?? DEFAULT_ANDROID_PACKAGE,
          sha256_cert_fingerprints: android.sha256CertFingerprints,
        },
      },
    ]);
  });

  // Apple fetches this through its CDN without redirects; it must be JSON at
  // exactly this path (no .json extension).
  app.get('/.well-known/apple-app-site-association', async (_request, reply) => {
    const ios = links?.ios;
    if (!ios) throw apiError('not_found', 'No iOS app is associated with this instance.');
    return reply.headers(headers).send({
      applinks: {
        // `components` (iOS 13+) and `appID`/`paths` (older iOS) say the same.
        details: [
          {
            appIDs: ios.appIds,
            components: APP_LINK_PATHS.map((path) => ({
              '/': path,
              comment: 'Invites and map launches',
            })),
          },
          ...ios.appIds.map((appID) => ({ appID, paths: [...APP_LINK_PATHS] })),
        ],
      },
    });
  });
}
