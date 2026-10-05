import { mergeConfig } from 'vitest/config';
import base from '../../vitest.config.ts';
export default mergeConfig(base,{test:{setupFiles:['artifacts/leader-readiness/delay-connect.ts']}});
