import { t } from '../messages.ts';

/** Fit the form and protocol limits without splitting an authored grapheme. */
export function skinCopyName(name: string): string {
  const parts = Array.from(
    new Intl.Segmenter(undefined, { granularity: 'grapheme' }).segment(name),
    (part) => part.segment,
  );
  let result = t('{value0} copy', { value0: parts.join('') });
  while (result.length > 64 && parts.length) {
    parts.pop();
    result = t('{value0} copy', { value0: parts.join('') });
  }
  if (result.length > 64) throw new RangeError('Copy-name template exceeds the name limit');
  return result;
}
