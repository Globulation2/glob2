import { studioSession } from '../../components/studio/storage.ts';
import { BuildingAiStudioTurn, isValid } from '@glob2/protocol';

export function readSavedTurn(key: string): BuildingAiStudioTurn | null {
  try {
    const text = studioSession.getItem(key);
    if (!text) return null;
    const value: unknown = JSON.parse(text);
    if (isValid(BuildingAiStudioTurn, value)) return value;
  } catch {
    // Storage may be blocked, or an old/incomplete browser record may remain.
  }
  rememberTurn(key, null);
  return null;
}

/** The in-memory UUID remains authoritative when browser storage is unavailable. */
export function rememberTurn(key: string, turn: BuildingAiStudioTurn | null) {
  try {
    if (turn) studioSession.setItem(key, JSON.stringify(turn));
    else studioSession.removeItem(key);
  } catch {
    // Reload recovery is best effort; storage must never prevent sending/retrying.
  }
}
