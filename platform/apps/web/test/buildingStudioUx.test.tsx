// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import type { ReactNode } from 'react';
import { AiBuildingStudio } from '../src/pages/AiBuildingStudio.tsx';
const mocked = vi.hoisted(() => ({ available: 0, request: vi.fn() }));
vi.mock('../src/state.tsx', () => ({ useSession: () => ({ account: { id: 'owner' } }) }));
vi.mock('../src/router.tsx', () => ({
  useRouter: () => ({ navigate: vi.fn(), location: { search: new URLSearchParams() } }),
  Link: ({ children, to }: { children: ReactNode; to: string }) => <a href={to}>{children}</a>,
}));
vi.mock('../src/api.ts', async (importOriginal) => ({
  ...(await importOriginal<object>()),
  request: mocked.request,
}));
vi.mock('../src/pages/BuildingStudio.tsx', () => ({ BuildingEditor: () => <p>Manual editor</p> }));
vi.mock('../src/pages/building-ai/Preview.tsx', () => ({
  BuildingPreview: () => <p>Saved building</p>,
}));
vi.mock('../src/pages/building-ai/useStudioSnapshot.ts', () => ({
  BUILDING_STUDIO_ROOT: '/api/v1/ai-building-studio',
  useStudioSnapshot: () => ({
    wallet: { enabled: true, available: mocked.available, reserved: 0, packs: [] },
    thread: { title: 'Hospital', messages: [], requests: [], revisions: [], references: [] },
    draft: { id: 'draft', revision: 'revision', package: {} },
    projects: [],
    refresh: vi.fn(),
  }),
}));
beforeEach(() => {
  sessionStorage.clear();
  mocked.available = 0;
  mocked.request.mockReset();
  HTMLDialogElement.prototype.showModal = function () {
    this.open = true;
  };
  HTMLDialogElement.prototype.close = function () {
    this.open = false;
    this.dispatchEvent(new Event('close'));
  };
});
afterEach(cleanup);
it.each(['click', 'Enter'])(
  'opens Building credits on zero-balance %s without dispatching or losing the prompt',
  (method) => {
    render(<AiBuildingStudio id="project" />);
    const prompt = screen.getByRole('textbox', { name: /Describe your creation/ });
    fireEvent.change(prompt, { target: { value: 'Build a healing hospital.' } });
    if (method === 'click') fireEvent.click(screen.getByRole('button', { name: /^Send$/ }));
    else fireEvent.keyDown(prompt, { key: 'Enter' });
    expect(screen.getByRole('dialog', { name: 'Building credits' })).toBeTruthy();
    expect(mocked.request).not.toHaveBeenCalled();
    expect((prompt as HTMLTextAreaElement).value).toBe('Build a healing hospital.');
  },
);
