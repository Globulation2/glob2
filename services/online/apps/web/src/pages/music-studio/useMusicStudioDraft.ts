import { studioSession } from '../../components/studio/storage.ts';
import { useCallback, useEffect, useState } from 'react';
import type { MusicStudioSettings } from '@glob2/protocol';

export interface Pending {
  path: string;
  body: { id: string; text?: string; settings?: MusicStudioSettings; parent?: string };
}
interface Design {
  settings: MusicStudioSettings;
  parent?: string;
}
function readSaved<T>(key: string): T | undefined {
  try {
    return (JSON.parse(studioSession.getItem(key) ?? 'null') as T | null) ?? undefined;
  } catch {
    return undefined;
  }
}

/** Account/project-scoped state survives refresh and the same-tab checkout round trip. */
export function useMusicStudioDraft(accountId: string, id?: string) {
  const suffix = `${accountId}:${id ?? 'new'}`;
  const draftKey = `music-studio-draft:${suffix}`;
  const pendingKey = `music-studio-pending:${suffix}`;
  const settingsKey = `music-studio-settings:${suffix}`;
  const [draft, setDraft] = useState(() => studioSession.getItem(draftKey) ?? '');
  const [pending, updatePending] = useState(() => readSaved<Pending>(pendingKey));
  const [design, setDesign] = useState<Design>(() => {
    const saved = readSaved<Design>(settingsKey);
    return {
      settings: saved?.settings ?? { pipeline: 'acoustic-v1', seed: 0 },
      parent: saved?.parent,
    };
  });
  useEffect(() => {
    studioSession.setItem(draftKey, draft);
  }, [draftKey, draft]);
  useEffect(() => {
    studioSession.setItem(settingsKey, JSON.stringify(design));
  }, [settingsKey, design]);
  // Persist before sending, rather than waiting for an effect after the network call.
  const setPending = useCallback(
    (value: Pending | undefined) => {
      if (value) studioSession.setItem(pendingKey, JSON.stringify(value));
      else studioSession.removeItem(pendingKey);
      updatePending(value);
    },
    [pendingKey],
  );
  return {
    draftKey,
    draft,
    setDraft,
    pending,
    setPending,
    settings: design.settings,
    parent: design.parent,
    setSettings: (settings: MusicStudioSettings) =>
      setDesign((current) => ({ ...current, settings })),
    setParent: (parent: string | undefined) => setDesign((current) => ({ ...current, parent })),
  };
}

/** Remove only this conversation's browser state after server deletion succeeds. */
export function clearMusicStudioDraft(accountId: string, threadId: string) {
  const suffix = `${accountId}:${threadId}`;
  for (const kind of ['draft', 'pending', 'settings', 'autosend'])
    studioSession.removeItem(`music-studio-${kind}:${suffix}`);
  const checkoutKey = `music-studio-checkout:${accountId}`;
  if (studioSession.getItem(checkoutKey) === threadId) {
    studioSession.removeItem(checkoutKey);
    studioSession.removeItem(`music-studio-checkout-balance:${accountId}`);
  }
}
