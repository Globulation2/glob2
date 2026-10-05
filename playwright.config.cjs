const path=require('node:path');
const root=path.resolve(__dirname,'../..');
const config=require(path.join(root,'browser/playwright.config.js'));
module.exports={...config,testDir:path.join(root,'browser/tests'),outputDir:path.join(__dirname,'managed-results'),reporter:[['list']],timeout:180000,workers:1,use:{...config.use,baseURL:'http://127.0.0.1:8795'},webServer:{command:'python3 browser/serve.py 8795 --bind 127.0.0.1 --directory build/emscripten/client/release',cwd:root,url:'http://127.0.0.1:8795',reuseExistingServer:false}};
