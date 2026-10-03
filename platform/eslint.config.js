// @ts-check
import js from '@eslint/js';
import tseslint from 'typescript-eslint';
import reactHooks from 'eslint-plugin-react-hooks';
import globals from 'globals';

export default tseslint.config(
  { ignores: ['**/node_modules/**', '**/dist/**', 'packages/protocol/fixtures/**'] },
  js.configs.recommended,
  ...tseslint.configs.strict,
  {
    languageOptions: { globals: globals.node },
    rules: {
      '@typescript-eslint/consistent-type-imports': 'error',
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_' }],
      // Apps are deployables; code two of them share belongs in a package
      // (packages/play for the match domain, packages/core for plumbing).
      'no-restricted-imports': [
        'error',
        {
          patterns: [
            {
              group: [
                '@glob2/api',
                '@glob2/api/*',
                '@glob2/worker',
                '@glob2/worker/*',
                '@glob2/engine-agent',
                '@glob2/engine-agent/*',
                '@glob2/web',
                '@glob2/web/*',
              ],
              message:
                'Apps must not import other apps: move the shared code into a package (packages/play or packages/core).',
            },
          ],
        },
      ],
    },
  },
  {
    // Tests and fixture scripts index into data they just built.
    files: ['**/test/**/*.{ts,tsx}', '**/scripts/**/*.ts'],
    rules: { '@typescript-eslint/no-non-null-assertion': 'off' },
  },
  {
    // The engine agent's end-to-end test runs a real platform-api replica
    // (its test harness) to exercise the internal engine API over HTTP.
    files: ['apps/engine-agent/test/queue.test.ts'],
    rules: { 'no-restricted-imports': 'off' },
  },
  {
    files: ['apps/web/**/*.{ts,tsx}'],
    languageOptions: { globals: globals.browser },
    plugins: { 'react-hooks': reactHooks },
    rules: reactHooks.configs.recommended.rules,
  },
);
