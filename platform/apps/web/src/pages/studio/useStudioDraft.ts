import { studioSession } from '../../components/studio/storage.ts';
import { useCallback, useEffect, useState } from 'react';
import type { StudioSettings } from '@glob2/protocol';

export interface Pending {
  path: string;
  body: { id: string; text?: string; settings?: StudioSettings; parent?: string };
}
interface Design {
  settings: StudioSettings;
  parent?: string;
  fresh?: boolean;
}
function readSaved<T>(key: string): T | undefined {
  try {
    return (JSON.parse(studioSession.getItem(key) ?? 'null') as T | null) ?? undefined;
  } catch {
    return undefined;
  }
}

/** Account/project-scoped state survives refresh and the same-tab checkout round trip. */
export function useStudioDraft(accountId: string, id?: string) {
  const suffix = `${accountId}:${id ?? 'new'}`;
  const draftKey = `studio-draft:${suffix}`;
  const pendingKey = `studio-pending:${suffix}`;
  const settingsKey = `studio-settings:${suffix}`;
  const [draft, setDraft] = useState(() => studioSession.getItem(draftKey) ?? '');
  const [pending, updatePending] = useState(() => readSaved<Pending>(pendingKey));
  const [design, setDesign] = useState<Design>(() => {
    const saved = readSaved<Design>(settingsKey);
    return {
      settings: saved?.settings ?? { width: 256, height: 256, players: 4 },
      parent: saved?.parent,
      fresh: saved?.fresh ?? false,
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
    fresh: design.fresh,
    setSettings: (settings: StudioSettings) => setDesign((current) => ({ ...current, settings })),
    setParent: (parent: string | undefined) =>
      setDesign((current) => ({ ...current, parent, fresh: !parent })),
  };
}
