import Fastify from 'fastify';
import { afterEach, expect, it } from 'vitest';
import { DEFAULT_INSTANCE_CONFIG } from '@glob2/core';
import { publicFeatures, registerPublicFeatureGuards } from '../src/publicFeatures.ts';

const apps: ReturnType<typeof Fastify>[] = [];
afterEach(async () => {
  await Promise.all(apps.splice(0).map((app) => app.close()));
});

it('advertises only enabled tools and requires the designer for sales', () => {
  expect(publicFeatures(DEFAULT_INSTANCE_CONFIG)).toEqual(['queue.multi']);
  expect(
    publicFeatures({
      ...DEFAULT_INSTANCE_CONFIG,
      skinDesigner: { enabled: false, salesEnabled: true },
    }),
  ).toEqual(['queue.multi']);
  expect(
    publicFeatures({
      ...DEFAULT_INSTANCE_CONFIG,
      skinDesigner: { enabled: true, salesEnabled: true },
    }),
  ).toEqual(['queue.multi', 'skins.designer', 'skins.sales']);
});

it('blocks direct disabled creation and checkout while keeping libraries and recovery reachable', async () => {
  const app = Fastify();
  apps.push(app);
  // Only config is used by this isolated hook; integration tests supply real services.
  app.decorate('services', {
    config: { instance: DEFAULT_INSTANCE_CONFIG },
  } as unknown as typeof app.services);
  registerPublicFeatureGuards(app);
  const blocked = [
    '/api/v1/map-studio/threads',
    '/api/v1/music-studio/checkout',
    '/api/v1/terrain-studio/threads/a/requests',
    '/api/v1/ai-building-studio/threads',
    '/api/v1/ai-studio/projects',
    '/api/v1/generator-studio/projects/a/check',
    '/api/v1/skins/draft',
    '/api/v1/skins/publish',
    '/api/v1/skins/designs/a/use',
    '/api/v1/skins/checkout',
  ];
  const allowed = [
    '/api/v1/ai-studio/stripe',
    '/api/v1/generator-studio/reconcile',
    '/api/v1/map-studio/threads/a/requests/b/cancel',
    '/api/v1/ai-studio/projects/a/stop',
    '/api/v1/generator-studio/projects/a/run-result',
    '/api/v1/skins/stripe-webhook',
    '/api/v1/maps/publish',
    '/api/v1/ais',
    '/api/v1/generators',
    '/api/v1/skins/equip',
  ];
  for (const url of [...blocked, ...allowed]) app.post(url, async () => ({ ok: true }));
  app.get('/api/v1/ai-studio/projects', async () => ({ ok: true }));
  app.delete('/api/v1/ai-studio/projects/a', async () => ({ ok: true }));
  for (const url of blocked)
    expect((await app.inject({ method: 'POST', url })).statusCode, url).toBe(503);
  for (const url of allowed)
    expect((await app.inject({ method: 'POST', url })).statusCode, url).toBe(200);
  expect((await app.inject({ method: 'GET', url: '/api/v1/ai-studio/projects' })).statusCode).toBe(
    200,
  );
  expect(
    (await app.inject({ method: 'DELETE', url: '/api/v1/ai-studio/projects/a' })).statusCode,
  ).toBe(200);
});
