require('./guard.cjs').guard();
const path = require('node:path');
const root = path.resolve(__dirname, '../../..');
// Retain tracked assertions; allow the observed host's cold/reload startup 90 seconds.
const helpers = require(path.join(root,'browser/tests/main-menu'));
const originalWait = helpers.waitForMainMenu;
helpers.waitForMainMenu = (page, options={}) => originalWait(page, {timeout:90000,...options});
module.exports = {
  testDir: path.join(root, 'browser/tests'),
  testMatch: 'bulgarian.spec.js',
  outputDir: path.join(__dirname, 'test-results'),
  timeout: 180000, expect: {timeout: 45000}, workers: 1, fullyParallel: false, retries: 0,
  reporter: [['list'], ['json', {outputFile:path.join(__dirname,'tests.json')}], ['html', {outputFolder:path.join(__dirname,'html-report'),open:'never'}]],
  use: {baseURL:'http://127.0.0.1:42771',viewport:{width:1200,height:900},trace:'retain-on-failure',screenshot:'only-on-failure'},
  projects:[{name:'chromium',use:{browserName:'chromium'}}]
};
