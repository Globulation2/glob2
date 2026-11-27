// Reads a non-fatal error report (error.json, protocol ErrorReport): a failure
// the game survived, such as a map that fails to load. It has no stack; its
// group is its category and message with numbers and quoted names blanked, so
// one failure in many maps or matches is one group.
import { ErrorReport } from '@glob2/protocol';
import { Value } from 'typebox/value';
import { UnreadableCrash, type Symbolicated } from './minidump.ts';

/** The message with the parts that vary between occurrences blanked. */
export function errorSignature(category: string, message: string): string {
  const shape = message
    .replace(/"[^"]*"|'[^']*'/g, '…')
    .replace(/0x[0-9a-f]+/gi, '#')
    .replace(/[0-9]+/g, '#')
    .replace(/\s+/g, ' ')
    .trim()
    .slice(0, 300);
  return `error ${category}: ${shape}`;
}

export function readErrorReport(text: string): Symbolicated {
  let report: ErrorReport;
  try {
    const parsed: unknown = JSON.parse(text);
    if (!Value.Check(ErrorReport, parsed)) throw new Error('schema');
    report = parsed;
  } catch {
    throw new UnreadableCrash('error.json is not an ErrorReport');
  }
  return {
    frames: [],
    reason: `${report.category}: ${report.message}`.slice(0, 200),
    signature: errorSignature(report.category, report.message),
  };
}
