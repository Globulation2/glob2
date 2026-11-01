import { t } from '../messages.ts';

/** Reserve room for the translated copy label without splitting an authored grapheme. */
export function skinCopyName(name: string): string {
  const parts = Array.from(
    new Intl.Segmenter(undefined, { granularity: 'grapheme' }).segment(name),
    (part) => part.segment,
  );
  let result = t('{value0} copy', { value0: parts.join('') });
  while (Array.from(result).length > 64 && parts.length) {
    parts.pop();
    result = t('{value0} copy', { value0: parts.join('') });
  }
  return result;
}
