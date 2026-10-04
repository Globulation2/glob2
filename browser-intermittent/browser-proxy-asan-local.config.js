const path=require('node:path');
module.exports={...require('../../browser/playwright.config'),testDir:path.join(__dirname,'browser-proxy-asan-local'),outputDir:path.join(__dirname,'browser-proxy-asan-local-results'),reporter:'list',timeout:600000,expect:{timeout:120000}};
