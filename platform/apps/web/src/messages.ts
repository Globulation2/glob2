import * as localization from '../../../packages/i18n/src/index.ts';
function displayParams(
  params?: Record<string, unknown>,
): Record<string, string | number> | undefined {
  const rtl =
    localization.locales.find((item) => item.code === localization.getLocale())?.dir === 'rtl';
  return (
    params &&
    Object.fromEntries(
      Object.entries(params).map(([key, value]) => [
        key,
        typeof value === 'number'
          ? value
          : rtl
            ? `\u2068${String(value ?? '')}\u2069`
            : String(value ?? ''),
      ]),
    )
  );
}
export function t(source: string | undefined, params?: Record<string, unknown>): string {
  return localization.t(source ?? '', displayParams(params));
}
export function tp(
  singular: string,
  plural: string,
  count: number,
  params?: Record<string, unknown>,
): string {
  return localization.tp(singular, plural, count, displayParams(params));
}
export function translateError(error: unknown): string {
  if (
    error &&
    typeof error === 'object' &&
    'body' in error &&
    error.body &&
    typeof error.body === 'object'
  ) {
    const body = error.body as { messageParams?: Record<string, unknown> };
    return localization.translateError({
      ...error,
      body: { ...body, messageParams: displayParams(body.messageParams) },
    });
  }
  return localization.translateError(error);
}

export const getLocale = localization.getLocale;
export const formatNumber = localization.formatNumber;
export const formatDate = localization.formatDate;
export const formatRelative = localization.formatRelative;

/** Keep notices in the source language so an open page can change locale later. */
export function message(source: string): string {
  return source;
}
export function displayMessage(value: unknown): string {
  return typeof value === 'string' ? t(value) : translateError(value);
}

/** Carry source and parameters until an error is presented in the current locale. */
export class MessageError extends Error {
  readonly body: { messageKey: string; messageParams?: Record<string, string | number> };
  constructor(source: string, params?: Record<string, unknown>) {
    const values =
      params &&
      Object.fromEntries(
        Object.entries(params).map(([key, value]) => [
          key,
          typeof value === 'number' ? value : String(value ?? ''),
        ]),
      );
    super(
      source.replace(/\{([A-Za-z][A-Za-z0-9_]*)\}/g, (token, key: string) =>
        values?.[key] === undefined ? token : String(values[key]),
      ),
    );
    this.name = 'MessageError';
    this.body = { messageKey: source, messageParams: values };
  }
}

/** Translate recognized display enums while retaining their original wire values. */
const STATUS_LABELS: Record<string, string> = {
  pass: 'Passed',
  fail: 'Failed',
  skip: 'Skipped',
  waived: 'Waived',
  'not-evaluated': 'Not evaluated',
  inspected: 'Inspected',
  inspecting: 'Inspecting',
  converting: 'Converting',
  withdrawn: 'Withdrawn',
  uncertain: 'Uncertain',
  refused: 'Refused',
  open: 'Open',
  in_match: 'In match',
  closed: 'Closed',
  diverged: 'Diverged',
  unverifiable: 'Unverifiable',
  not_applicable: 'Not applicable',
  finished: 'Finished',
  reference: 'Reference',
  generated: 'Generated',
  crop: 'Crop',
  categorical: 'Categorical',
  preview: 'Preview',
  ready: 'Ready',
  pending: 'Pending',
  queued: 'Queued',
  waiting: 'Waiting',
  running: 'Running',
  starting: 'Starting',
  processing: 'Processing',
  uploading: 'Uploading',
  submitted: 'Submitted',
  completed: 'Completed',
  complete: 'Complete',
  succeeded: 'Succeeded',
  failed: 'Failed',
  cancelled: 'Cancelled',
  cancel_requested: 'Cancellation requested',
  reconciling: 'Reconciling',
  stopped: 'Stopped',
  retrying: 'Retrying',
  retryable: 'Can be retried',
  unknown: 'Unknown',
  draft: 'Draft',
  published: 'Published',
  private: 'Private',
  public: 'Public',
  unlisted: 'Unlisted',
  active: 'Active',
  banned: 'Banned',
  deleted: 'Deleted',
  disabled: 'Disabled',
  hidden: 'Hidden',
  resolved: 'Resolved',
  dismissed: 'Dismissed',
  rejected: 'Rejected',
  accepted: 'Accepted',
  valid: 'Valid',
  invalid: 'Invalid',
  verified: 'Verified',
  unverified: 'Unverified',
  verification_failed: 'Verification failed',
  passed: 'Passed',
  skipped: 'Skipped',
  not_run: 'Not run',
  ok: 'OK',
  error: 'Error',
  warn: 'Warning',
  warning: 'Warning',
  info: 'Information',
  missing: 'Missing',
  unavailable: 'Unavailable',
  degraded: 'Degraded',
  connected: 'Connected',
  disconnected: 'Disconnected',
  won: 'Won',
  lost: 'Lost',
  draw: 'Draw',
  abandoned: 'Abandoned',
  ended: 'Ended',
  charged: 'Charged',
  refunded: 'Refunded',
  reserved: 'Reserved',
  delivered: 'Delivered',
  applied: 'Applied',
  needs_attention: 'Needs attention',
  guest: 'Guest',
  registered: 'Registered',
  moderator: 'Moderator',
  admin: 'Administrator',
  user: 'User',
  purchase: 'Purchase',
  payment: 'Payment',
  refund: 'Refund',
  usage: 'Usage',
  reservation: 'Reservation',
  release: 'Release',
  terrain: 'Terrain',
  resource: 'Resource',
};
export function statusLabel(value: unknown): string {
  const source = typeof value === 'string' ? value : String(value ?? '');
  return t(Object.hasOwn(STATUS_LABELS, source) ? STATUS_LABELS[source] : source);
}

export interface ValidationMessage {
  source: string;
  params?: Record<string, unknown>;
}
export function validationMessage(
  source: string,
  params?: Record<string, unknown>,
): ValidationMessage {
  return { source, params };
}

/** Normalize known system captions while preserving authored titles and diagnostics. */
export function fixedCaption(label: string | undefined): string {
  if (!label) return '';
  const labels = new Set([
    'Generated layout',
    'Crop selection',
    'Selected terrain',
    'Playable terrain',
    'Delivered map',
    'Ready to play',
    'Converted terrain',
    'Native terrain preview · awaiting checks',
    'Generated layout · intermediate image',
    'Selected map area',
    'Cropped layout · intermediate image',
    'Validation report',
    'Game-rendered scene and resource gallery',
    'Native validation report',
    'Engine import and artwork',
    'Engine definitions and artwork',
    'Requested size and players',
    'Valid colony starts',
    'Resource growth enabled',
    'Connected walking routes',
    'Opening economy measured',
  ]);
  if (labels.has(label)) return t(label);
  const templates: [RegExp, string][] = [
    [/^Landscape reference ([1-9][0-9]*)$/, 'Landscape reference {count}'],
    [/^Composition source · edit ([1-9][0-9]*)$/, 'Composition source · edit {count}'],
    [/^Candidate ([1-9][0-9]*) execution report$/, 'Candidate {count} execution report'],
    [/^Candidate ([1-9][0-9]*) score$/, 'Candidate {count} score'],
    [/^Candidate ([1-9][0-9]*) · validated$/, 'Candidate {count} · validated'],
    [/^Candidate ([1-9][0-9]*) · needs repairs$/, 'Candidate {count} · needs repairs'],
    [/^Candidate ([1-9][0-9]*) report$/, 'Candidate {count} report'],
    [/^Colony ([1-9][0-9]*): building space$/, 'Colony {count}: building space'],
    [/^Colony ([1-9][0-9]*): starter food$/, 'Colony {count}: starter food'],
    [/^Colony ([1-9][0-9]*): starter timber$/, 'Colony {count}: starter timber'],
    [/^Colony ([1-9][0-9]*): renewable food ground$/, 'Colony {count}: renewable food ground'],
  ];
  for (const [pattern, key] of templates) {
    const match = pattern.exec(label);
    if (match) return t(key, { count: Number(match[1]) });
  }
  return label;
}
export function artifactLabel(artifact: { label: string; kind?: string }): string {
  if (artifact.kind === 'reference') {
    const reference = /^Reference sheet ([1-9][0-9]*)$/.exec(artifact.label);
    if (reference) return `${t('Reference')} ${formatNumber(Number(reference[1]))}`;
  }
  if (artifact.kind === 'source') {
    const decor = /^(.*\S) decor source$/.exec(artifact.label);
    if (decor) return t('{value0} decor source', { value0: decor[1] });
    const source = /^(.*\S) source$/.exec(artifact.label);
    if (source) return t('{value0} source', { value0: source[1] });
  }
  return fixedCaption(artifact.label);
}
