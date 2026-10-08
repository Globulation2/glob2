import { apiError } from '../errors.ts';

// Exact actions keep new product/financial actions private until explicitly reviewed.
export const MODERATION_AUDIT_ACTIONS = [
  'account.rename',
  'account.mute',
  'account.unmute',
  'content.hide',
  'content.restore',
  'report.resolve',
  'map.hide',
  'map.unhide',
  'map.report.resolved',
  'map.report.dismissed',
  'ai.hide',
  'ai.unhide',
  'ai.report.resolved',
  'ai.report.dismissed',
  'building.hide',
  'building.unhide',
  'building.report.resolve',
  'set:hide',
  'set:unhide',
  'set:resolve-report',
  'skin.disable',
  'skin.enable',
  'skin.report.resolve',
  'music.moderate',
] as const;

/** Date-only end filters include that UTC day; full timestamps remain exclusive. */
export function auditDate(value?: string, end = false): Date | undefined {
  if (!value) return undefined;
  const day = /^\d{4}-\d{2}-\d{2}$/.test(value),
    date = new Date(value);
  if (
    value.length > 100 ||
    !Number.isFinite(date.getTime()) ||
    (day && date.toISOString().slice(0, 10) !== value)
  )
    throw apiError('bad_request', 'Invalid date.');
  if (day && end) date.setUTCDate(date.getUTCDate() + 1);
  return date;
}
