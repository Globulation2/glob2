import type { Page } from '@playwright/test';

/**
 * Creation studios are opt-in per instance (apps/api/src/publicFeatures.ts) and the
 * seed server enables none of them. A spec that drives a studio through mocked studio
 * APIs adds the studio to the instance's public features, keeping the rest of the
 * seeded instance as served.
 */
export async function enableFeatures(page: Page, ...features: string[]): Promise<void> {
  await page.route('**/api/v1/instance', async (route) => {
    const response = await route.fetch();
    const instance = (await response.json()) as { features?: string[] };
    await route.fulfill({
      response,
      json: { ...instance, features: [...new Set([...(instance.features ?? []), ...features])] },
    });
  });
}
