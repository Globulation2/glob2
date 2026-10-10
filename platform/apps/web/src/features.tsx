import { createContext, useContext, type ReactNode } from 'react';
import { useSession } from './state.tsx';

const FeatureContext = createContext<readonly string[]>([]);
const paths: Record<string, string> = {
  'map-studio': 'map-studio',
  'music-studio': 'music-studio',
  'terrain-studio': 'terrain-studio',
  'ai-building-studio': 'ai-building-studio',
  'ai-studio': 'ai-studio',
  'generator-studio': 'generator-studio',
  commander: 'commander',
  skins: 'skins.designer',
};

export function pathAvailable(path: string, features: readonly string[]): boolean {
  const feature = paths[path.split(/[/?#]/)[1] ?? ''];
  return !feature || features.includes(feature);
}

export function useFeatures() {
  return useContext(FeatureContext);
}

export function FeatureProvider({ children }: { children: ReactNode }) {
  const { instance } = useSession();
  return (
    <FeatureContext.Provider value={instance?.features ?? []}>{children}</FeatureContext.Provider>
  );
}
