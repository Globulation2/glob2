import { createContext } from 'react';

/** The collection uses the ordinary page shell; editing retains the studio shell. */
export const SkinWorkspaceContext = createContext<(editing: boolean) => void>(() => {});
