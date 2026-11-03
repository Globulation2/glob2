// Test tooling only; no game package imports this module.
const path = require('node:path');
const {execFileSync} = require('node:child_process');

exports.firefoxCaptureLaunchOptions = () => {
  if (process.platform !== 'linux') return {};
  const {firefox} = require('@playwright/test');
  const executablePath = execFileSync('python3', [
    path.join(__dirname, 'firefox_capture.py'),
    '--source', path.dirname(firefox.executablePath()),
    '--output-root', path.resolve(__dirname, '../build/browser-testing/firefox-compositor'),
  ], {encoding:'utf8'}).trim();
  return {executablePath};
};
