import type { FastifyInstance } from 'fastify';
import type { InstanceConfig } from '@glob2/core';
import { apiError } from './errors.ts';

const studios = {
  'map-studio': 'mapStudio',
  'music-studio': 'musicStudio',
  'terrain-studio': 'terrainStudio',
  'ai-building-studio': 'buildingStudio',
  'ai-studio': 'aiStudio',
  'generator-studio': 'generatorStudio',
} as const;

export function publicFeatures(config: InstanceConfig): string[] {
  return [
    'queue.multi',
    ...Object.entries(studios).flatMap(([route, key]) => (config[key]?.enabled ? [route] : [])),
    ...(config.hiveMind?.enabled ? ['commander'] : []),
    ...(config.skinDesigner?.enabled ? ['skins.designer'] : []),
    ...(config.skinDesigner?.enabled && config.skinDesigner.salesEnabled ? ['skins.sales'] : []),
  ];
}

/** Keep reads, deletion and payment/recovery callbacks working after shutdown. */
export function registerPublicFeatureGuards(app: FastifyInstance) {
  app.addHook('onRequest', async (request) => {
    if (['GET', 'HEAD', 'OPTIONS', 'DELETE'].includes(request.method)) return;
    const path = request.routeOptions.url ?? request.url.split('?')[0] ?? '';
    const config = app.services.config.instance;
    if (
      path === '/api/v1/skins/checkout' &&
      (!config.skinDesigner?.enabled || !config.skinDesigner.salesEnabled)
    )
      throw apiError('unavailable', 'Skin purchases are not available on this instance.');
    for (const [route, key] of Object.entries(studios)) {
      if (!path.startsWith('/api/v1/' + route + '/')) continue;
      if (/\/(stripe|reconcile|cancel|stop|fail|run-result)$/.test(path)) return;
      if (!config[key as (typeof studios)[keyof typeof studios]]?.enabled)
        throw apiError('unavailable', 'This creation tool is not available on this instance.');
    }
    if (
      !config.skinDesigner?.enabled &&
      (path === '/api/v1/skins/draft' ||
        path === '/api/v1/skins/publish' ||
        /^\/api\/v1\/skins\/designs(?:\/|$)/.test(path))
    )
      throw apiError('unavailable', 'The skin designer is not available on this instance.');
  });
}
