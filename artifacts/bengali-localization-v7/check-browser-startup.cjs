// Review evidence only; does not modify source files or claim mobile OS coverage.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const net = require('node:net');
const {spawn, execFileSync} = require('node:child_process');
const root = process.cwd();
const {chromium} = require(path.join(root, 'platform/node_modules/@playwright/test'));
const {clickMainMenu, clickControl, clickByLabel, clickSettingsDone} = require(path.join(root, 'browser/tests/main-menu.js'));
const build = path.resolve(process.argv[2] || 'build/emscripten/client/debug/dev-dev_fast-true-linker-auto');
const evidence = path.resolve(process.argv[3] || 'artifacts/bengali-localization/browser-runtime');
fs.mkdirSync(evidence, {recursive: true});
assert(!fs.existsSync(path.join(evidence, 'startup-report.json')), 'Use a new evidence directory to preserve the prior attempt');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fileSha = file => sha(fs.readFileSync(file));
const json = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n');
const bridgeFactory = text => { const start=text.indexOf('Module.textBridge = (() => {'); const end=text.indexOf('\n})();', start)+'\n})();'.length; assert(start>=0 && end>start); return text.slice(start,end); };
const nativeFile = fs.readFileSync(path.join(root, 'data/texts.bn.txt'));
const lines = nativeFile.toString('utf8').trimEnd().split('\n');
const native = {};
for (let i = 0; i < lines.length; i += 2) native[lines[i]] = lines[i + 1];
const onlineFile = path.join(root, 'platform/packages/i18n/locales/bn.json');
const fontFile = path.join(root, 'data/fonts/scripts/NotoSansBengali-Regular.ttf');
const expected = {textBridgeFactory: sha(bridgeFactory(fs.readFileSync(path.join(root,'browser/shell.html'),'utf8'))), nativeCatalog: sha(nativeFile), onlineCatalog: fileSha(onlineFile), bengaliFont: fileSha(fontFile), coreSans: fileSha(path.join(root, 'browser/assets/sans-core.ttf')), languageList: fileSha(path.join(root, 'data/texts.list.txt')),
  i18nRuntime: fileSha(path.join(root, 'browser/i18n.js')), launcherCSS: sha(fs.readFileSync(path.join(root, 'platform/packages/i18n/script-fonts.css'), 'utf8').replaceAll('../../../data/fonts/scripts/', 'fonts/'))};
const producerRecord = process.env.GLOB2_BROWSER_PRODUCER_RECORD || path.join(root, 'artifacts/bengali-localization/browser-final-production-provenance.json');
assert(fs.existsSync(producerRecord), 'Finalize and bind actual producer provenance before runtime gate checks');
const report = {producerProvenance: {path: producerRecord, sha256: fileSha(producerRecord)}, scope: 'Linux Chromium real WebAssembly software-rendered serial/threaded desktop/phone startup plus threaded WebGL2 desktop; not Android/iOS device evidence',
  captureRevision: execFileSync('git', ['rev-parse', 'HEAD'], {encoding: 'utf8'}).trim(),
  productionRevision: json(producerRecord).package_production_input_revision,
  cppProductionRevision: json(producerRecord).cpp_production_input_revision,
  sourceHistory: {path: 'artifacts/bengali-localization-v7/browser-source-history.json',
    sha256: fileSha(path.join(root, 'artifacts/bengali-localization-v7/browser-source-history.json'))}, build,
  node: process.version, harnessSha256: fileSha(__filename), mainMenuHelperSha256: fileSha(path.join(root, 'browser/tests/main-menu.js')),
  playwrightVersion: json(path.join(root, 'platform/node_modules/@playwright/test/package.json')).version, expected, packageFiles: {}, cases: [], automatedPassed: false, visualInspection: 'pending'};
const frozen = json(path.join(root,'artifacts/bengali-localization-v7/expected-producer.json'));
assert.equal(report.captureRevision,frozen.revision);
assert.equal(report.productionRevision,frozen.revision);
assert.equal(report.cppProductionRevision,frozen.revision);
assert.equal(expected.nativeCatalog,frozen.catalog_sha256.native);
assert.equal(expected.onlineCatalog,frozen.catalog_sha256.online);
report.frozenSourceBinding={path:'artifacts/bengali-localization-v7/expected-producer.json',sha256:fileSha(path.join(root,'artifacts/bengali-localization-v7/expected-producer.json'))};
for (const name of ['identity.json', 'threaded/identity.json', 'index.html', 'index.js', 'index.wasm', 'index.data',
  'threaded/index.js', 'threaded/index.wasm', 'threaded/index.data', 'i18n.js', 'locales/bn.json', 'script-fonts.css',
  'fonts/NotoSansBengali-Regular.ttf', 'fonts/LICENSE-Noto.txt', 'hive-runtime.js','hive-runtime.wasm','hive-worker.js',
  'music-runtime.js','music-runtime.wasm','music-worker.js','music-output.js',
  'recording-runtime.js','recording-runtime.wasm','recording-worker.js','recording-storage.js','recording-video.js']) {
  assert(fs.existsSync(path.join(build, name)), `Missing package file ${name}`);
  report.packageFiles[name] = fileSha(path.join(build, name));
}
assert.equal(sha(bridgeFactory(fs.readFileSync(path.join(build,'index.html'),'utf8'))), expected.textBridgeFactory, 'Packaged actual DOM bridge must match current reviewed source');
assert.equal(execFileSync('git',['status','--porcelain'],{encoding:'utf8'}).trim(), '', 'Runtime capture starts on clean source');
assert.equal(report.packageFiles['locales/bn.json'], expected.onlineCatalog);
assert.equal(report.packageFiles['i18n.js'], expected.i18nRuntime);
assert.equal(report.packageFiles['script-fonts.css'], expected.launcherCSS);
assert.equal(report.packageFiles['fonts/NotoSansBengali-Regular.ttf'], expected.bengaliFont);
report.identity = json(path.join(build, 'identity.json'));
report.threadedIdentity = json(path.join(build, 'threaded/identity.json'));
for (const identity of [report.identity,report.threadedIdentity]) {
  assert.equal(identity.target,'web'); assert.equal(identity.client_profile,'full');
  for (const feature of ['authoring_links','commander','community_ai','community_generators']) assert.equal(identity.client_features[feature],true);
}
assert.equal(report.packageFiles['fonts/LICENSE-Noto.txt'],fileSha(path.join(root,'data/fonts/LICENSE-Noto.txt')));
report.packagedDependencyLicenses = {};
for (const name of ['recording','opus']) {
  const directory=path.join(build,'licenses',name); assert(fs.statSync(directory).isDirectory());
  const files=fs.readdirSync(directory); assert(files.length>0);
  report.packagedDependencyLicenses[name]=Object.fromEntries(files.map(file=>[file,fileSha(path.join(directory,file))]));
}
report.scriptFontManifest = json(path.join(root, 'data/fonts/scripts/manifest.json'));
write(path.join(evidence, 'startup-report.json'), report);
// Popup rows publish the entire screen as root; wheel over the actual popup,
// not the root center used by the general test helper.
async function clickLanguage(page, label) {
  for (let attempt=0; attempt<30; ++attempt) {
    const state=await page.evaluate(label=>{
      const s=glob2Diagnostics.snapshot();
      const match=Object.entries(s.controls).find(([key,c])=>key.startsWith('popup/') && c.label===label);
      if (!match) throw new Error('Missing actual language row '+label);
      return {key:match[0],...match[1]};
    },label);
    if (state.visible?.h>=8) { await clickControl(page,state.key); return; }
    const box=await page.locator('#canvas').boundingBox();
    const sx=box.width/state.surface.w, sy=box.height/state.surface.h;
    await page.mouse.move(box.x+(state.x+state.w/2)*sx,box.y+state.surface.h/2*sy);
    await page.mouse.wheel(0,state.y<20?-600:600);
    await page.waitForTimeout(200);
  }
  throw new Error('Language popup row never became visible: '+label);
}
async function freePort() {
  const socket = net.createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve));
  return port;
}
async function waitServer(url, server) {
  for (let attempt = 0; attempt < 120; attempt++) {
    if (server.exitCode !== null) throw new Error('Isolated browser host exited');
    try { if ((await fetch(url)).ok) return; } catch {}
    await new Promise(resolve => setTimeout(resolve, 250));
  }
  throw new Error('Browser host did not become ready');
}
function packagedBytes(manifest, packageName, filename) {
  const entry = manifest.packages.find(item => item.name === packageName);
  assert(entry, `Missing package ${packageName}`);
  for (const part of entry.parts) for (const [name, start, end] of part.files) if (name === filename) {
    const file = fs.openSync(path.join(build, part.url), 'r');
    try { const bytes = Buffer.alloc(end - start); assert.equal(fs.readSync(file, bytes, 0, bytes.length, start), bytes.length); return bytes; }
    finally { fs.closeSync(file); }
  }
  throw new Error(`Missing ${filename} in ${packageName}`);
}
async function waitForMainMenu(page, {timeout=120000, record, name}={}) {
  const deadline=Date.now()+timeout;
  let unexpectedSince=0;
  while (Date.now()<deadline) {
    const state=await page.evaluate(() => typeof glob2Diagnostics === 'object' ? glob2Diagnostics.snapshot() : {screen:'loading',controls:{}});
    const screen=state.screen+' '+state.screenClass;
    if (screen.includes('MainMenuScreen')) return;
    const choices=screen.includes('AudioPreferenceScreen') ? ['audio.silence','audio.picture.silence']
      : screen.includes('HandPreferenceScreen') ? ['hand.right','hand.grip.right']
      : screen.includes('IntroductionOfferScreen') ? ['intro/menu','intro/continue'] : [];
    const key=choices.find(key=>state.controls?.[key]?.enabled);
    if (key) {
      const interaction={screen,control:key,bounds:state.controls[key]};
      if (record) (record.startupInteractions ??= []).push(interaction);
      console.log(name ?? 'startup','dismiss actual startup screen',screen,key);
      if (name && /AudioPreferenceScreen|HandPreferenceScreen/.test(screen))
        await page.screenshot({path:path.join(evidence,name+(screen.includes('AudioPreferenceScreen')?'-audio-preference.png':'-hand-preference.png')),fullPage:true});
      await clickControl(page,key);
      await page.waitForFunction(previous => {
        const current=glob2Diagnostics.snapshot();
        return current.screen+' '+current.screenClass !== previous;
      },screen,{timeout:15000});
      unexpectedSince=0;
    } else {
      if (!/loading|undefined/.test(screen)) {
        unexpectedSince ||= Date.now();
        if (Date.now()-unexpectedSince>20000) throw new Error('Unexpected startup screen: '+JSON.stringify(state));
      }
      await page.waitForTimeout(100);
    }
  }
  throw new Error('Startup deadline reached; actual snapshot: '+JSON.stringify(await page.evaluate(()=>glob2Diagnostics.snapshot())));
}
async function settlePresentedUI(page) {
  const loop=await page.evaluate(()=>glob2Diagnostics.snapshot().loop);
  await page.waitForFunction(loop=>glob2Diagnostics.snapshot().loop>=loop+3,loop,{timeout:20000});
  await page.waitForTimeout(500);
}
async function readVFS(page, file) {
  return Buffer.from(await page.evaluate(file => {
    const bytes = FS.readFile(file); let encoded = '';
    for (let i = 0; i < bytes.length; i += 32768)
      encoded += String.fromCharCode(...bytes.subarray(i, i + 32768));
    return btoa(encoded);
  }, file), 'base64');
}
(async () => {
  const port = await freePort(); const origin = `http://127.0.0.1:${port}`;
  const serverLog = fs.openSync(path.join(evidence, 'server.log'), 'w');
  const server = spawn('python3', ['browser/serve.py', String(port), '--bind', '127.0.0.1', '--directory', build],
    {cwd: root, stdio: ['ignore', serverLog, serverLog]});
  let browser;
  try {
    await waitServer(origin + '/index.html', server);
    browser = await chromium.launch({headless: true, timeout: 120000,
      args: ['--disable-dev-shm-usage', '--use-angle=swiftshader']});
    report.browserVersion = browser.version();
    const cases = ['serial', 'threaded'].flatMap(variant => [false, true].map(phone => ({variant, phone, renderer:'software'})));
    cases.push({variant:'threaded', phone:false, renderer:'webgl2'});
    const selected=process.env.GLOB2_BENGALI_CASES?.split(',');
    const caseName=({variant,phone,renderer})=>`${variant}-${phone?'phone':'desktop'}${renderer==='webgl2'?'-webgl2':''}`;
    report.requiredCaseNames=cases.map(caseName);
    if (selected) assert(selected.every(name=>report.requiredCaseNames.includes(name)));
    report.selectedCaseNames=selected ?? report.requiredCaseNames;
    report.allFiveCasesInThisReport=report.selectedCaseNames.length === 5;
    for (const {variant, phone, renderer} of cases.filter(item=>report.selectedCaseNames.includes(caseName(item)))) {
      const name = `${variant}-${phone ? 'phone' : 'desktop'}${renderer === 'webgl2' ? '-webgl2' : ''}`;
      const context = await browser.newContext({locale: 'bn-BD', viewport: phone ? {width: 390, height: 844} : {width: 1280, height: 900},
        deviceScaleFactor: phone ? 2 : 1, isMobile: phone, hasTouch: phone, serviceWorkers: 'block'});
      const page = await context.newPage(); page.setDefaultTimeout(120000);
      const record = {name, variant, renderer, viewport: page.viewportSize(), deviceScaleFactor: phone ? 2 : 1,
        freshStorage: true, externalRequestsBlocked: [], pageErrors: [], console: [], requests: []};
      report.cases.push(record);
      let releaseWasm; const wasmGate = new Promise(resolve => releaseWasm = resolve);
      try {
        record.localeSelection = variant === 'serial' && !phone ? 'navigator bn-BD' : 'locale cookie bn';
        if (record.localeSelection === 'locale cookie bn')
          await context.addCookies([{name: 'glob2_locale', value: 'bn', url: origin, sameSite: 'Lax'}]);
        const cdp = await context.newCDPSession(page);
        await cdp.send('Network.enable'); await cdp.send('Network.setCacheDisabled', {cacheDisabled: true});
        page.on('pageerror', error => record.pageErrors.push(String(error)));
        page.on('console', message => record.console.push({type: message.type(), text: message.text()}));
        page.on('response', response => record.requests.push({url: response.url(), status: response.status(), type: response.headers()['content-type']}));
        await page.route('**/*', async route => {
          const url = new URL(route.request().url());
          if (url.origin !== origin && !['blob:', 'data:'].includes(url.protocol)) {
            record.externalRequestsBlocked.push(url.href); return route.abort();
          }
          if (url.pathname.endsWith('/index.wasm')) await wasmGate;
          return route.continue();
        });
        const fontFetched = page.waitForResponse(response => response.url().endsWith('/fonts/NotoSansBengali-Regular.ttf') && response.status() === 200);
        const catalogFetched = page.waitForResponse(response => response.url().endsWith('/locales/bn.json') && response.status() === 200);
        // Preserve the primary failure if an earlier assertion closes the page.
        fontFetched.catch(() => {}); catalogFetched.catch(() => {});
        const url = `${origin}/index.html?renderer=${renderer}${variant === 'serial' ? '&threads=serial' : ''}`;
        console.log(name, 'cold startup', url);
        const navigation = await page.goto(url, {waitUntil: 'domcontentloaded', timeout: 120000});
        record.isolationHeaders = navigation.headers();
        assert.equal(record.isolationHeaders['cross-origin-opener-policy'], 'same-origin');
        assert.equal(record.isolationHeaders['cross-origin-embedder-policy'], 'require-corp');
        await page.waitForFunction(() => globalThis.Glob2I18n?.getLocale() === 'bn');
        await page.locator('#loading-language').waitFor({state: 'visible'});
        assert.equal(await page.locator('#loading-language').inputValue(), 'bn');
        assert.equal(await page.locator('html').getAttribute('lang'), 'bn');
        if (variant === 'serial' && !phone) {
          await page.locator('#loading-language').selectOption('en');
          await page.waitForFunction(() => Glob2I18n.getLocale() === 'en');
          assert.equal((await context.cookies(origin)).find(cookie => cookie.name === 'glob2_locale')?.value,'en');
          await page.locator('#loading-language').selectOption('bn');
          await page.waitForFunction(() => Glob2I18n.getLocale() === 'bn');
          const cookie=(await context.cookies(origin)).find(cookie => cookie.name === 'glob2_locale');
          assert.equal(cookie?.value,'bn'); assert(cookie.expires > Date.now()/1000);
          record.launcherLanguageSwitch={selected:['en','bn'],persistedCookie:cookie};
        }

        await page.evaluate(async () => { await document.fonts.load('16px "Glob2 Scripts"', 'বাংলা ক্ষুদ্র শ্রমিক'); await document.fonts.ready; });
        const fontResponse = await fontFetched; const catalogResponse = await catalogFetched;
        record.httpFont = {url: fontResponse.url(), headers: fontResponse.headers(), sha256: sha(await fontResponse.body())};
        record.httpCatalog = {url: catalogResponse.url(), sha256: sha(await catalogResponse.body())};
        assert.equal(record.httpFont.sha256, expected.bengaliFont);
        assert.equal(record.httpCatalog.sha256, expected.onlineCatalog);
        await cdp.send('DOM.enable'); await cdp.send('CSS.enable');
        const {root: documentRoot} = await cdp.send('DOM.getDocument');
        const {nodeId} = await cdp.send('DOM.querySelector', {nodeId: documentRoot.nodeId, selector: '#loading-status'});
        record.launcherPlatformFonts = (await cdp.send('CSS.getPlatformFontsForNode', {nodeId})).fonts;
        assert(record.launcherPlatformFonts.some(font => font.isCustomFont && font.glyphCount > 0 && /Bengali|Glob2 Scripts/i.test(font.familyName + ' ' + font.postScriptName)), 'Bengali launcher glyphs must use the packaged custom font');
        record.launcher = await page.evaluate(() => ({language: Glob2I18n.getLocale(), text: document.querySelector('#loading-status').textContent,
          fonts: [...document.fonts].filter(font => font.family.includes('Glob2 Scripts')).map(font => ({family: font.family, status: font.status, unicodeRange: font.unicodeRange})),
          bodyWidth: document.documentElement.scrollWidth, viewportWidth: innerWidth}));
        assert(/[\u0980-\u09ff]/u.test(record.launcher.text));
        assert(record.launcher.bodyWidth <= record.launcher.viewportWidth);
        await page.screenshot({path: path.join(evidence, name + '-launcher.png'), fullPage: true});
        await page.waitForFunction(() => Module.glob2AssetManifest?.packages?.length > 0);
        const assetManifest = await page.evaluate(() => Module.glob2AssetManifest);
        record.packageClosure = {manifestSha256: sha(JSON.stringify(assetManifest)),
          coreSans: sha(packagedBytes(assetManifest, 'core', '/data/fonts/sans.ttf')),
          coreBengaliFont: sha(packagedBytes(assetManifest, 'core', '/data/fonts/scripts/NotoSansBengali-Regular.ttf')),
          fullBengaliCatalog: sha(packagedBytes(assetManifest, 'translations', '/data/texts.bn.txt')),
          cjkFontSeparate: assetManifest.packages.some(item => item.name === 'font-cjk'),
          derivedSourceMetadata: json(path.join(root, 'browser/assets/sources.json'))['browser/assets/sans-core.ttf']};
        assert.equal(record.packageClosure.coreSans, expected.coreSans, 'Core must use the current derived subset, not full-font fallback');
        assert.equal(record.packageClosure.coreBengaliFont, expected.bengaliFont);
        assert.equal(record.packageClosure.fullBengaliCatalog, expected.nativeCatalog);
        assert.equal(record.packageClosure.cjkFontSeparate, true);
        await page.evaluate(() => {
          const original=Module.textBridge.field;
          const seen=new Map();
          globalThis.__glob2BengaliTextBounds=[];
          Module.textBridge.field=function(...args) {
            const key=[args[0],args[6],args[9] ?? 0].join(':');
            if (!seen.has(key)) {
              if (seen.size >= 1000) throw new Error('Bengali QA input-boundary observation budget exceeded');
              const boundary={owner:args[0],exclusiveMaximumBytes:args[6],inclusiveMaximumCodepoints:args[9] ?? 0};
              seen.set(key,boundary);globalThis.__glob2BengaliTextBounds.push(boundary);
            }
            return original.apply(this,args);
          };
        });
        releaseWasm();
        await waitForMainMenu(page, {timeout: 300000,record,name});
        await page.waitForFunction(() => Module.glob2Assets?.states?.translations === 'ready', null, {timeout: 300000});
        record.mainMenu = await page.evaluate(() => ({diagnostics: glob2Diagnostics.snapshot(),
          onlineLanguage: Glob2I18n.getLocale(), preferredLocales: ENV.SDL_PREFERRED_LOCALES,
          crossOriginIsolated, sharedMemory: typeof SharedArrayBuffer !== 'undefined' && HEAPU8.buffer instanceof SharedArrayBuffer}));
        assert.equal(record.mainMenu.diagnostics.executionMode, variant);
        assert.equal(record.mainMenu.diagnostics.renderer, renderer, 'Verify actual selected renderer, not only URL intent');
        assert.equal(record.mainMenu.sharedMemory, variant === 'threaded');
        assert.equal(record.mainMenu.crossOriginIsolated, true);
        assert.equal(record.mainMenu.onlineLanguage, 'bn');
        assert.equal(record.mainMenu.preferredLocales, 'bn');
        assert.equal(record.mainMenu.diagnostics.controls['menu/3']?.label, native['[custom game]']);
        record.vfs = {bengaliFont: sha(await readVFS(page, '/data/fonts/scripts/NotoSansBengali-Regular.ttf')),
          bengaliCatalog: sha(await readVFS(page, '/data/texts.bn.txt')), languageList: sha(await readVFS(page, '/data/texts.list.txt'))};
        assert.equal(record.vfs.bengaliFont, expected.bengaliFont);
        assert.equal(record.vfs.bengaliCatalog, expected.nativeCatalog);
        assert.equal(record.vfs.languageList, expected.languageList);
        await settlePresentedUI(page);
        await page.screenshot({path: path.join(evidence, name + '-main.png'), fullPage: true});
        await clickMainMenu(page, 'settings');
        await page.waitForFunction(() => /SettingsScreen/.test(glob2Diagnostics.snapshot().screen + glob2Diagnostics.snapshot().screenClass));
        record.settings = await page.evaluate(() => glob2Diagnostics.snapshot());
        assert(Object.values(record.settings.controls).some(control => /[\u0980-\u09ff]/u.test(control.label || '')));
        await settlePresentedUI(page);
        await page.screenshot({path: path.join(evidence, name + '-settings.png'), fullPage: true});
        // Exercise the real UTF-8 browser edit bridge with a conjunct and vowels.
        if (record.settings.controls['nav.current']) {
          await clickControl(page, 'nav.current');
          await clickByLabel(page, /^popup\/\d+$/, native['[settings Language & player]']);
        } else await clickControl(page, 'nav.5');
        if (renderer === 'webgl2') {
          await clickControl(page,'player.language');
          await clickLanguage(page,'English');
          await page.waitForFunction(() => FS.readFile('/home/web_user/.glob2/preferences.txt',{encoding:'utf8'}).split('\n').includes('language=en'));
          await page.waitForFunction(() => glob2Diagnostics.snapshot().controls['nav.5']?.label === 'Language & player');
          await page.waitForTimeout(500);
          await page.screenshot({path:path.join(evidence,name+'-language-english.png'),fullPage:true});
          await clickControl(page,'player.language');
          await clickLanguage(page,native['[language]']);
          await page.waitForFunction(() => FS.readFile('/home/web_user/.glob2/preferences.txt',{encoding:'utf8'}).split('\n').includes('language=bn'));
          record.nativeLanguageSwitch={selected:['en','bn'],savedPreference:'bn',renderer:(await page.evaluate(()=>glob2Diagnostics.snapshot())).renderer};
          assert.equal(record.nativeLanguageSwitch.renderer,'webgl2');
          await page.waitForFunction(label => glob2Diagnostics.snapshot().controls['nav.5']?.label === label,native['[settings Language & player]']);
          await page.waitForTimeout(500);
          await page.screenshot({path:path.join(evidence,name+'-language-bengali.png'),fullPage:true});
          record.opaquePanel = await require(path.join(root,'artifacts/bengali-localization-v7/webgl-text-audit/check-opaque-panel.cjs')).checkOpaquePanel(page,path.join(evidence,'opaque-panel')); 
        }
        await clickControl(page, 'player.name');
        const input = page.locator('input[data-i18n-aria="Game text field"]');
        await input.waitFor({state: 'visible'});
        const usernameBoundary=await page.evaluate(() => __glob2BengaliTextBounds.find(boundary=>boundary.exclusiveMaximumBytes === 33 && boundary.inclusiveMaximumCodepoints === 32));
        assert(usernameBoundary,'Record actual C++ username byte/character bounds at the live JS boundary');
        record.usernameCppBoundary=usernameBoundary;
        const bengaliName = 'ক্ষুদ্র ১২';
        assert(Buffer.byteLength(bengaliName, 'utf8') < 32);
        await input.fill(bengaliName);
        assert.equal(await input.inputValue(), bengaliName);
        await input.press('Home');
        assert.equal(await input.evaluate(element => element.selectionStart), 0);
        await input.press('End');
        assert.equal(await input.evaluate(element => element.selectionEnd), bengaliName.length);
        record.textInput = await input.evaluate(element => ({value: element.value, ariaLabel: element.getAttribute('aria-label'),
          fontFamily: getComputedStyle(element).fontFamily, selectionStart: element.selectionStart, selectionEnd: element.selectionEnd}));
        await page.screenshot({path: path.join(evidence, name + '-bengali-input.png'), fullPage: true});
        await input.press('Enter');
        await page.waitForFunction(expectedName => {
          try { return FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'}).split('\n').includes('username=' + expectedName); }
          catch { return false; }
        }, bengaliName);
        record.textInput.persistedUtf8Exactly = true;
        await clickControl(page, 'player.name');
        const overLimit = 'ক'.repeat(8) + 'ক্ষ';
        const wholePrefix = 'ক'.repeat(8);
        await input.fill(overLimit);
        record.textInput.byteLimitPaste = {input: overLimit, inputBytes: Buffer.byteLength(overLimit),
          actual: await input.inputValue(), expected: wholePrefix};
        assert.equal(record.textInput.byteLimitPaste.actual, wholePrefix, 'Byte limit must preserve the complete Bengali conjunct');
        await input.fill('x'.repeat(32));
        assert.equal(await input.inputValue(), 'x'.repeat(32), 'Native username cap permits 32 ASCII bytes');
        await input.fill('x'.repeat(33));
        assert.equal(await input.inputValue(), 'x'.repeat(32), 'Exclusive DOM maximum33 truncates ASCII overflow to32');
        record.textInput.asciiByteLimit = {maximumStoredBytes:32, accepted32:true, rejectedExtraByte:true};
        const composingValue = await input.evaluate((element, value) => {
          element.dispatchEvent(new CompositionEvent('compositionstart', {bubbles: true, data: value}));
          element.value = value;
          element.dispatchEvent(new InputEvent('input', {bubbles: true, inputType: 'insertCompositionText', data: value, isComposing: true}));
          return element.value;
        }, overLimit);
        assert.equal(composingValue, overLimit, 'Do not truncate an active composition');
        await input.evaluate(element => element.dispatchEvent(new CompositionEvent('compositionend', {bubbles: true, data: element.value})));
        assert.equal(await input.inputValue(), wholePrefix, 'Committed composition must respect complete grapheme boundaries');
        record.textInput.compositionEventBridgePreservedClusters = true;
        if (renderer === 'webgl2') {
          await input.fill(bengaliName);
          await input.press('Enter');
          record.hudFixturePlayerName=bengaliName;
          await clickSettingsDone(page);
          await waitForMainMenu(page,{timeout:120000});
          await clickMainMenu(page,'custom');
          await page.waitForFunction(() => glob2Diagnostics.snapshot().customGameReady === true,null,{timeout:300000});
          record.customLobby=await page.evaluate(()=>glob2Diagnostics.snapshot());
          await page.screenshot({path:path.join(evidence,name+'-custom-lobby.png'),fullPage:true});
          await clickControl(page,'start');
          await page.waitForFunction(() => glob2Diagnostics.snapshot().tick > 25,null,{timeout:300000});
          record.hud=await page.evaluate(()=>glob2Diagnostics.snapshot());
          assert.equal(record.hud.renderer,'webgl2');
          assert.equal(record.hud.executionMode,'threaded');
          await page.screenshot({path:path.join(evidence,name+'-custom-hud.png'),fullPage:true});
          // A real native character-only field exercises the C++ -> DOM bounds wire.
          await page.locator('#canvas').press('Escape',{delay:80});
          await clickControl(page,'save');
          await page.waitForFunction(() => {const c=glob2Diagnostics.snapshot().controls;return c.files && c.name && c.ok && c.cancel;},null,{timeout:20000});
          await clickControl(page,'name');
          const saveInput=page.locator('input[data-i18n-aria="Game text field"]');
          await saveInput.waitFor({state:'visible',timeout:20000});
          await page.waitForFunction(() => __glob2BengaliTextBounds.some(boundary=>boundary.exclusiveMaximumBytes === 0 && boundary.inclusiveMaximumCodepoints === 64),null,{timeout:20000});
          const saveBoundary=await page.evaluate(() => __glob2BengaliTextBounds.find(boundary=>boundary.exclusiveMaximumBytes === 0 && boundary.inclusiveMaximumCodepoints === 64));
          assert(saveBoundary,'Record actual C++ Save dialog character-only bound at the live JS boundary');
          record.saveCppBoundary=saveBoundary;
          const saveName='ক'.repeat(43);
          await saveInput.fill(saveName);
          assert.equal(await saveInput.inputValue(),saveName,'A valid43character129bytefilename must remain intact');
          await saveInput.fill('ক'.repeat(65));
          assert.equal(await saveInput.inputValue(),'ক'.repeat(64),'Actual native Save dialog forwards its inclusive64character limit');
          await saveInput.fill(saveName);
          record.genericCharacterField={nativeMaximumCharacters:64,name:saveName,codepoints:43,utf8Bytes:Buffer.byteLength(saveName),validValueIntact:true,overflowClippedTo64WholeCharacters:true};
          await page.screenshot({path:path.join(evidence,name+'-bengali-save-input.png'),fullPage:true});
          await clickControl(page,'ok');
          await page.waitForFunction(name => glob2Diagnostics.saves().some(file=>file.startsWith(name)) && glob2Diagnostics.snapshot().persistence === 'persisted',saveName,{timeout:300000});
          const saved=await page.evaluate(name=>glob2Diagnostics.saves().find(file=>file.startsWith(name)),saveName);
          const bytes=await readVFS(page,'/home/web_user/.glob2/games/'+saved);
          const saveEvidence='bengali-unicode-name'+saved.slice(saveName.length);
          fs.writeFileSync(path.join(evidence,saveEvidence),bytes);
          record.genericCharacterField.persistedSave={actualFilename:saved,bytes:bytes.length,sha256:sha(bytes),evidenceFile:saveEvidence};

        }



        assert.equal(record.pageErrors.length, 0, 'No browser page errors during Bengali startup');
        assert(!record.console.some(message => /missing glyph|HarfBuzz.*unavailable|script font.*missing/i.test(message.text)));
        record.automatedPassed = true;
        console.log(name, 'passed');
      } catch (error) {
        record.error = error.stack || String(error);
        try { record.failureSnapshot=await page.evaluate(()=>glob2Diagnostics.snapshot()); } catch {}
        try { await page.screenshot({path: path.join(evidence, name + '-failure.png'), fullPage: true}); } catch {}
        throw error;
      } finally {
        try { record.cppTextBoundaries=await page.evaluate(() => globalThis.__glob2BengaliTextBounds ?? []); } catch {}
        releaseWasm(); write(path.join(evidence, name + '.json'), record);
        write(path.join(evidence, 'startup-report.json'), report);
        await context.close();
      }
    }
    assert.equal(execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(), report.captureRevision, 'Capture source revision remains stable');
    assert.equal(execFileSync('git',['status','--porcelain'],{encoding:'utf8'}).trim(), '', 'Capture source remains clean');
    report.automatedPassed = true;
    report.screenshots = fs.readdirSync(evidence).filter(name => name.endsWith('.png')).map(name => ({path: name, sha256: fileSha(path.join(evidence, name))}));
    write(path.join(evidence, 'startup-report.json'), report);
    console.log('Selected Bengali runtime cases passed:',report.selectedCaseNames.join(', '),'; screenshots require visual review.');
  } finally {
    if (browser) await browser.close();
    server.kill('SIGTERM'); fs.closeSync(serverLog);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
