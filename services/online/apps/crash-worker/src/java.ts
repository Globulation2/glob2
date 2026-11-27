// Reads an uncaught Java exception from the Android game (java-exception.txt,
// written by Glob2Activity.captureJavaCrashes): a "thread: <name>" line, then
// the stack as Throwable.printStackTrace writes it. Java frames are already
// symbolic; the game's own are those in org.globulation2.
import type { CrashFrame } from '@glob2/protocol';
import { UnreadableCrash, type Symbolicated } from './minidump.ts';
import { clampFrame, inGame, MAX_FRAMES } from './signature.ts';

/** The module name of Java frames. */
export const JAVA_MODULE = 'java';

const FRAME = /^\s*at ([^\s(]+)\(([^)]*)\)\s*$/;

/** The frames of the thrown exception (not its causes), innermost first. */
export function javaFrames(text: string): CrashFrame[] {
  const frames: CrashFrame[] = [];
  for (const line of text.split('\n')) {
    if (/^\s*Caused by:/.test(line)) break;
    const match = FRAME.exec(line);
    if (!match) continue;
    const location = match[2] ?? '';
    const colon = location.lastIndexOf(':');
    const lineNumber = colon > 0 ? Number(location.slice(colon + 1)) : NaN;
    const file = Number.isInteger(lineNumber) ? location.slice(0, colon) : location;
    const frame = {
      module: JAVA_MODULE,
      offset: '0x0',
      function: match[1] ?? '',
      ...(file && file !== 'Unknown Source' && file !== 'Native Method' ? { file } : {}),
      ...(Number.isInteger(lineNumber) && lineNumber >= 0 ? { line: lineNumber } : {}),
    };
    frames.push(clampFrame({ ...frame, inGame: inGame(frame) }));
    if (frames.length >= MAX_FRAMES) break;
  }
  return frames;
}

export function symbolicateJava(text: string): Symbolicated {
  const lines = text.split('\n').filter((line) => !line.startsWith('thread: '));
  const header = lines.find((line) => line.trim() && !FRAME.test(line));
  const frames = javaFrames(text);
  if (!header || !frames.length)
    throw new UnreadableCrash('java-exception.txt holds no Java stack');
  return { frames, reason: header.trim().slice(0, 200) };
}
