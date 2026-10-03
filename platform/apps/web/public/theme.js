/* global document */
// Applies a stored theme choice before the first paint (theme.tsx owns it
// afterwards). A file rather than an inline script, so the Content-Security-Policy
// (deploy/Caddyfile) can allow scripts from this origin only.
try {
  const theme = localStorage.getItem('glob2-theme');
  if (theme === 'light' || theme === 'dark') {
    document.documentElement.setAttribute('data-theme', theme);
  }
} catch {
  // Storage blocked (private mode): keep the system theme.
}
