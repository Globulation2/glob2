import { t } from '../../messages.ts';
export type StudioRequestState =
  'queued' | 'running' | 'checking' | 'uncertain' | 'failed' | 'completed';
/** Transport states remain distinct in their domain; the shell uses these presentation states. */
export function requestState(status: string): StudioRequestState {
  if (status === 'ready' || status === 'completed') return 'completed';
  if (status === 'failed' || status === 'cancelled') return 'failed';
  if (status === 'uncertain') return 'uncertain';
  if (status === 'queued') return 'queued';
  if (status === 'checking' || status === 'importing') return 'checking';
  return 'running';
}
export function cancellationReason(status: string): string | undefined {
  return status === 'uncertain'
    ? t('The provider outcome must be reconciled before cancellation.')
    : status === 'dispatched'
      ? t('Wait for the provider call to return before cancelling.')
      : undefined;
}
