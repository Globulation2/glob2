const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf("const preferencesPath =");
const end = shell.indexOf('// After the menu is up', start);
function profile(language, online) {
  const contents = language === null ? '' : `language=${language}\nhighResolutionArtwork=0\n`;
  const context = vm.createContext({
    ENV:{}, navigator:{languages:['en-US']},
    Glob2I18n:{getLocale:() => online},
    FS:{analyzePath:() => ({exists:language !== null}),readFile:() => contents},
  });
  vm.runInContext(shell.slice(start, end), context);
  return context;
}
test('the online choice supplies a first-visit SDL hint and startup language', () => {
  const context = profile(null, 'pt-BR');
  context.applyOnlineLanguage();
  assert.equal(context.ENV.SDL_PREFERRED_LOCALES, 'pt_BR');
  assert.equal(context.interfaceLanguage(), 'pt-BR');
});
test('Norwegian online startup uses the core font and preserves the saved game choice', () => {
  const context = profile(null, 'nb');
  context.applyOnlineLanguage();
  assert.equal(context.ENV.SDL_PREFERRED_LOCALES, 'nb');
  assert.equal(context.interfaceLanguage(), 'nb');
  assert.equal(context.cjkInterface(), false);
  const saved = profile('nb', 'de');
  saved.applyOnlineLanguage();
  assert.equal(saved.ENV.SDL_PREFERRED_LOCALES, undefined);
  assert.equal(saved.interfaceLanguage(), 'nb');
});
test('a saved game language takes priority and receives no online override', () => {
  const context = profile('de', 'zh-Hant');
  context.applyOnlineLanguage();
  assert.equal(context.ENV.SDL_PREFERRED_LOCALES, undefined);
  assert.equal(context.interfaceLanguage(), 'de');
  assert.equal(context.cjkInterface(), false);
});
test('Bengali is handed to the game and a saved Bengali choice persists', () => {
  const firstVisit = profile(null, 'bn');
  firstVisit.applyOnlineLanguage();
  assert.equal(firstVisit.ENV.SDL_PREFERRED_LOCALES, 'bn');
  assert.equal(firstVisit.interfaceLanguage(), 'bn');
  assert.equal(firstVisit.cjkInterface(), false);
  const returning = profile('bn', 'en');
  returning.applyOnlineLanguage();
  assert.equal(returning.ENV.SDL_PREFERRED_LOCALES, undefined);
  assert.equal(returning.interfaceLanguage(), 'bn');
});
test('both Chinese scripts and Japanese/Korean require the full startup font', () => {
  for (const language of ['zh-Hans','zh-Hant','ja','ko']) {
    const context = profile(null, language);
    context.applyOnlineLanguage();
    assert.equal(context.cjkInterface(), true);
  }
  assert.equal(profile('zh-cn','en').cjkInterface(), true);
  assert.equal(profile('ar','en').cjkInterface(), false);
});
test('unavailable preference storage still permits the online startup language', () => {
  const context = profile(null, 'fa');
  context.FS.analyzePath = () => { throw Error('storage blocked'); };
  context.applyOnlineLanguage();
  assert.equal(context.ENV.SDL_PREFERRED_LOCALES, 'fa');
});
test('translated blocked-link labels isolate the technical URL from RTL text', () => {
  const context = vm.createContext({
    browserText:() => 'افتح {target} ↗',
    document:{
      createElement:tag => ({tag}),
      createTextNode:text => ({text}),
    },
  });
  const from = shell.indexOf('function updateOpenLink(');
  const to = shell.indexOf('const languagePicker =', from);
  vm.runInContext(shell.slice(from, to), context);
  const anchor = {replaceChildren:(...nodes) => { anchor.nodes = nodes; }};
  context.updateOpenLink(anchor, new URL('https://glob2.org/account'));
  assert.equal(anchor.nodes[0].text, 'افتح ');
  assert.equal(anchor.nodes[1].tag, 'bdi');
  assert.equal(anchor.nodes[1].dir, 'ltr');
  assert.equal(anchor.nodes[1].textContent, 'glob2.org/account');
  assert.equal(anchor.nodes[2].text, ' ↗');
});

test('Malay starts from the online choice with the core Latin font and preserves saved choices', () => {
  const context = profile(null, 'ms');
  context.applyOnlineLanguage();
  assert.equal(context.ENV.SDL_PREFERRED_LOCALES, 'ms');
  assert.equal(context.interfaceLanguage(), 'ms');
  assert.equal(context.cjkInterface(), false);
  const saved = profile('fr', 'ms');
  saved.applyOnlineLanguage();
  assert.equal(saved.ENV.SDL_PREFERRED_LOCALES, undefined);
  assert.equal(saved.interfaceLanguage(), 'fr');
});
