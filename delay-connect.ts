import pg from 'pg';
import { writeFileSync } from 'node:fs';
import { vi } from 'vitest';
const original = pg.Client.prototype.connect;
let attempts = 0;
vi.spyOn(pg.Client.prototype, 'connect').mockImplementation(function (this: pg.Client, ...args: any[]) {
 const name = (this as any).connectionParameters.application_name;
 if (name === 'glob2-leader-matchmaker-test' && ++attempts === 3) {
  writeFileSync(process.env.GLOB2_DELAY_PROOF!, "Restarted leader connect delayed 600ms\n");
  console.log("CONTROLLED: delaying restarted leader connection 600ms");
  return new Promise(resolve => setTimeout(resolve, 600)).then(() => original.apply(this, args as any));
 }
 return original.apply(this, args as any);
} as any);
