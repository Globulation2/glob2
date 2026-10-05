import { useEffect, useRef, useState } from 'react';

/** A delivery gets one reveal, even when the player revisits its version. */
export function useDeliveryCelebration(
  deliveredId: string | undefined,
  inspectedId: string | undefined,
  ready: boolean,
  following = true,
) {
  const observed = useRef(new Set<string>());
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const [revealing, setRevealing] = useState<string>();
  const [previousInspection, setPreviousInspection] = useState(inspectedId);
  // Leaving a reveal consumes it immediately, before another inspected frame paints.
  if (previousInspection !== inspectedId) {
    setPreviousInspection(inspectedId);
    if (revealing !== inspectedId) setRevealing(undefined);
  }
  useEffect(() => {
    // An inspected stage/version should not gain a delayed celebration on return.
    if (deliveredId && !following) observed.current.add(deliveredId);
    if (!deliveredId || deliveredId !== inspectedId || !ready || observed.current.has(deliveredId))
      return;
    observed.current.add(deliveredId);
    setRevealing(deliveredId);
    clearTimeout(timer.current);
    timer.current = setTimeout(() => setRevealing(undefined), 700);
  }, [deliveredId, inspectedId, ready, following]);
  useEffect(() => () => clearTimeout(timer.current), []);
  return revealing === inspectedId && ready;
}
