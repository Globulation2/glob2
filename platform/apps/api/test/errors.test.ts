import { describe, expect, it } from 'vitest';
import { apiError } from '../src/errors.ts';
describe('translatable API errors', () => {
  it('retains the legacy English message and machine code with additive translation metadata', () => {
    const error = apiError(
      'bad_request',
      'Keep at most {p0} versions.',
      { problem: 'limit' },
      { p0: 5 },
    );
    expect(error.body).toEqual({
      code: 'bad_request',
      message: 'Keep at most 5 versions.',
      messageKey: 'Keep at most {p0} versions.',
      messageParams: { p0: 5 },
      details: { problem: 'limit' },
    });
    expect(error.message).toBe('Keep at most 5 versions.');
    expect(error.statusCode).toBe(400);
  });
});
