export default {
  testDir: '../../platform/apps/web/e2e',
  testMatch: 'skins.spec.ts',
  outputDir: './test-results',
  workers: 1,
  timeout: 60000,
  use: {baseURL:'http://127.0.0.1:4281',launchOptions:{args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']}},
  projects: [
    {name:'desktop',use:{viewport:{width:1280,height:860}}},
    {name:'phone',use:{viewport:{width:393,height:851},isMobile:true,hasTouch:true}},
  ],
};
