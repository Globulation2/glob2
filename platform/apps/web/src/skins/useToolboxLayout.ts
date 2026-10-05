import { useCallback, useEffect, useRef, useState, type PointerEvent } from 'react';

const STORAGE_KEY = 'glob2-studio-toolbox';
type Position = { x: number; y: number };

export function useToolboxLayout(visible: boolean) {
  const toolbox = useRef<HTMLElement>(null);
  const dragging = useRef<{ id: number; x: number; y: number } | null>(null);
  const [collapsed, setCollapsed] = useState(
    () => window.innerWidth <= 599 && window.innerHeight < 700,
  );
  const [position, setPosition] = useState<Position | null>(() => {
    try {
      const saved: unknown = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? 'null');
      if (
        saved &&
        typeof saved === 'object' &&
        'x' in saved &&
        'y' in saved &&
        typeof saved.x === 'number' &&
        typeof saved.y === 'number' &&
        Number.isFinite(saved.x) &&
        Number.isFinite(saved.y)
      )
        return { x: saved.x, y: saved.y };
    } catch {
      /* Layout persistence is optional. */
    }
    return null;
  });
  const currentPosition = useRef(position);
  const move = useCallback((p: Position) => {
    const panel = toolbox.current;
    const stage = panel?.parentElement;
    const next = {
      x: Math.max(
        12,
        Math.min((stage?.clientWidth ?? window.innerWidth) - (panel?.offsetWidth ?? 244) - 12, p.x),
      ),
      y: Math.max(
        12,
        Math.min(
          (stage?.clientHeight ?? window.innerHeight) - (panel?.offsetHeight ?? 480) - 12,
          p.y,
        ),
      ),
    };
    currentPosition.current = next;
    setPosition(next);
  }, []);
  useEffect(() => {
    if (!visible) return;
    const fit = () => {
      if (currentPosition.current) move(currentPosition.current);
    };
    fit();
    window.addEventListener('resize', fit);
    const observer = new ResizeObserver(fit);
    if (toolbox.current) observer.observe(toolbox.current);
    return () => {
      window.removeEventListener('resize', fit);
      observer.disconnect();
      dragging.current = null;
    };
  }, [visible, move]);
  function finish(e: PointerEvent<HTMLElement>) {
    if (dragging.current?.id !== e.pointerId) return;
    dragging.current = null;
    try {
      if (currentPosition.current)
        localStorage.setItem(STORAGE_KEY, JSON.stringify(currentPosition.current));
    } catch {
      /* Layout persistence is optional. */
    }
  }
  return {
    toolbox,
    position,
    collapsed,
    setCollapsed,
    resetLayout() {
      currentPosition.current = null;
      setPosition(null);
      setCollapsed(false);
      try {
        localStorage.removeItem(STORAGE_KEY);
      } catch {
        /* Optional. */
      }
    },
    handleProps: {
      onPointerDown(e: PointerEvent<HTMLElement>) {
        if (
          dragging.current !== null ||
          e.button !== 0 ||
          window.innerWidth < 900 ||
          (e.target as HTMLElement).closest('button')
        )
          return;
        const panel = toolbox.current?.getBoundingClientRect();
        if (!panel) return;
        dragging.current = { id: e.pointerId, x: e.clientX - panel.left, y: e.clientY - panel.top };
        e.currentTarget.setPointerCapture(e.pointerId);
      },
      onPointerMove(e: PointerEvent<HTMLElement>) {
        const drag = dragging.current;
        const stage = toolbox.current?.parentElement?.getBoundingClientRect();
        if (!drag || drag.id !== e.pointerId || !stage) return;
        move({ x: e.clientX - stage.left - drag.x, y: e.clientY - stage.top - drag.y });
      },
      onPointerUp: finish,
      onPointerCancel: finish,
      onLostPointerCapture: finish,
    },
  };
}
