import { initialize } from '../../../packages/i18n/src/index.ts';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App.tsx';
import '@glob2/design-system/css/tokens.css';
import '@glob2/design-system/css/fonts.css';
import '@glob2/design-system/css/base.css';
import '@glob2/design-system/css/components.css';
import '@glob2/design-system/css/brand.css';
import './styles/base.css';
import './styles/components.css';
import './styles/layout.css';
import './styles/home.css';
import './styles/pages.css';
import './styles/skin-studio.css';

const root = document.getElementById('root');
if (!root) throw new Error('missing #root');
await initialize();
createRoot(root).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
