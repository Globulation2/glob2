import { cursorTime } from '../http/cursorTime.ts';
import { apiError } from '../errors.ts';

export function decodeCursor(
  value?: string,
): { at: Date; exactAt: string; id: string } | undefined {
  if (!value) return undefined;
  try {
    if (value.length > 512) throw new Error();
    const pair: unknown = JSON.parse(Buffer.from(value, 'base64url').toString());
    if (
      !Array.isArray(pair) ||
      pair.length !== 2 ||
      typeof pair[0] !== 'string' ||
      typeof pair[1] !== 'string'
    )
      throw new Error();
    const at = new Date(pair[0]);
    if (!Number.isFinite(at.getTime()) || !/^[a-zA-Z0-9:-]{1,100}$/.test(pair[1]))
      throw new Error();
    return { at, exactAt: cursorTime(pair[0]), id: pair[1] };
  } catch {
    throw apiError('bad_request', 'Invalid cursor.');
  }
}

export function encodeCursor(at: Date | string, id: string): string {
  return Buffer.from(JSON.stringify([cursorTime(at), id])).toString('base64url');
}

export function pageSize(value?: string): number {
  if (value === undefined) return 50;
  const size = Number(value);
  if (!Number.isInteger(size) || size < 1 || size > 100)
    throw apiError('bad_request', 'Limit must be between 1 and 100.');
  return size;
}
