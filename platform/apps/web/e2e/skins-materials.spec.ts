import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test, type Page } from '@playwright/test';
import { COLONY_SKIN_MATERIALS } from '@glob2/protocol';

// Review evidence for the material catalogue at studio resolution: the swatch
// strips and the worker filled with every material, written under artifacts/.
const output = resolve(import.meta.dirname, '../../../../artifacts/skins/materials');

async function fillMaterial(page: Page, name: string) {
  await page.getByRole('button', { name: 'Patterns', exact: true }).click();
  const dialog = page.getByRole('dialog', { name: 'Patterns & fills' });
  await dialog.getByRole('button', { name: 'Whole model', exact: true }).click();
  await dialog.getByRole('button', { name: 'Solid', exact: true }).click();
  await dialog.getByRole('radio', { name, exact: true }).click();
  await dialog.getByRole('button', { name: 'Apply to Worker' }).click();
  await expect(page.getByRole('dialog')).toHaveCount(0);
}

test('every registered material previews on the worker and in the swatches', async ({
  page,
}, info) => {
  // The shader output does not depend on the device; one project is enough.
  test.skip(info.project.name !== 'desktop', 'desktop captures cover every material');
  // Each whole-model fill re-renders the dialog preview; software WebGL is slow.
  test.setTimeout(60_000 * COLONY_SKIN_MATERIALS.length);
  mkdirSync(output, { recursive: true });
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/skins');
  await expect(page.getByLabel('Skin name')).toBeEnabled();
  await page.getByRole('button', { name: 'Material', exact: true }).click();
  const swatches = page.getByRole('radiogroup', { name: 'Material' }).first();
  await expect(swatches.getByRole('radio')).toHaveCount(COLONY_SKIN_MATERIALS.length);
  await swatches.screenshot({ path: resolve(output, `${info.project.name}-swatches.png`) });
  const preview = page.getByLabel('Paint directly on the 3D worker model');
  await expect(preview).toHaveAttribute('data-frame', '0');
  const seen = new Map<string, string>();
  let last = (await preview.screenshot()).toString('base64');
  for (const material of COLONY_SKIN_MATERIALS) {
    await fillMaterial(page, material.name);
    // The preview redraws on its next animation frame after the fill lands;
    // a fresh document already wears the first material.
    if (material.id > 0)
      await expect
        .poll(async () => (await preview.screenshot()).toString('base64'), {
          message: `${material.key} changes the preview`,
        })
        .not.toBe(last);
    last = (
      await preview.screenshot({
        path: resolve(output, `${info.project.name}-worker-${material.key}.png`),
      })
    ).toString('base64');
    // Every material must look different from every other one.
    expect(seen.get(last), `${material.key} renders like ${seen.get(last)}`).toBeUndefined();
    seen.set(last, material.key);
  }
});
