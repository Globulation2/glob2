const {test,expect}=require("/home/bradley/.codex/worktrees/307ed29a-915d-4451-b89a-dd866c5cb3af/glob2/browser/node_modules/@playwright/test");
const old=require("/home/bradley/.codex/worktrees/307ed29a-915d-4451-b89a-dd866c5cb3af/glob2/browser/tests/main-menu");
const fixed=require("/home/bradley/.codex/worktrees/307ed29a-915d-4451-b89a-dd866c5cb3af/glob2/artifacts/ci-recovery/replay-save-repair/browser/tests/main-menu");
test('readiness polls through missing startup diagnostics without masking the main menu assertion',async({page})=>{
 await page.goto('about:blank');
 await page.evaluate(()=>{setTimeout(()=>{globalThis.glob2Diagnostics={snapshot:()=>({screen:'MainMenuScreen',screenClass:'MainMenuScreen',controls:{}})};},1000);});
 await expect(old.waitForMainMenu(page)).rejects.toThrow(/glob2Diagnostics|snapshot/);
 await fixed.waitForMainMenu(page);
 expect((await page.evaluate(()=>glob2Diagnostics.snapshot())).screenClass).toContain('MainMenuScreen');
});
