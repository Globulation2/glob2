// SPDX-License-Identifier: GPL-3.0-or-later
const { chromium } = require('playwright');
const assert = require('node:assert/strict');

(async () => {
    const browser = await chromium.launch({ args: [
        '--enable-features=Vulkan', '--use-vulkan=swiftshader',
        '--use-angle=vulkan', '--disable-vulkan-surface',
    ] });
    try {
        const page = await browser.newPage();
        const errors = [], consoleMessages = [];
        page.on('pageerror', error => errors.push(String(error)));
        page.on('console', message => consoleMessages.push(message.text()));
        await page.goto(process.argv[2] || 'http://127.0.0.1:8786/');
        await page.waitForFunction(() => window.proofReady);
        const cycles = [];
        for (let cycle = 0; cycle < 3; ++cycle) {
            const result = await page.evaluate(async cycle => {
                function replaceCanvas() {
                    const canvas = document.createElement('canvas');
                    canvas.id = 'canvas';
                    canvas.width = canvas.height = 64;
                    Module.canvas.replaceWith(canvas);
                    Module.canvas = canvas;
                }
                function call(name, ...args) {
                    if (!Module['_proof_' + name](...args))
                        throw Error(name + ': ' + UTF8ToString(Module._proof_error()));
                }
                async function capture() {
                    call('capture');
                    const deadline = performance.now() + 10000;
                    while (true) {
                        const done = Module._proof_poll();
                        if (done < 0) throw Error(UTF8ToString(Module._proof_error()));
                        if (done) return globalThis.proofPixel;
                        if (performance.now() > deadline) throw Error('Capture timed out');
                        await new Promise(resolve => setTimeout(resolve, 1));
                    }
                }
                if (cycle) replaceCanvas();
                call('init');
                const samples = [];
                for (const reduction of [1, 2, 8, 32]) {
                    for (const mixed of [0, 1]) {
                        call('terrain', reduction, mixed);
                        samples.push({ reduction, mixed, pixel: await capture() });
                        // Change the VAO/program before the next terrain pass. The
                        // target must still be owned when this mesh is submitted.
                        call('world');
                        call('trim');
                    }
                }
                call('resize', 128, 96);
                call('resize', 64, 64);
                call('terrain', 8, 1);
                samples.push({ resize: true, pixel: await capture() });
                call('shutdown');
                replaceCanvas();
                call('software');
                call('shutdown');
                return samples;
            }, cycle);
            result.forEach(sample => assert.deepEqual(sample.pixel, [0, 128, 0, 255]));
            cycles.push(result);
        }
        assert.deepEqual(errors, []);
        console.log(JSON.stringify({ cycles, errors, consoleMessages }, null, 2));
    } finally {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
