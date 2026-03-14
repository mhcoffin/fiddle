const { chromium } = require('playwright');

(async () => {
    try {
        const browser = await chromium.connectOverCDP('http://localhost:9222');
        console.log('Successfully connected to Fiddle WebView.');

        const context = browser.contexts()[0];
        const page = context.pages()[0];

        if (!page) {
            console.error('No active page found in WebView.');
            await browser.close();
            return;
        }

        const branchBtn = page.locator('.branch-btn').first();
        await branchBtn.waitFor({ state: 'visible', timeout: 5000 });
        let text = await branchBtn.textContent();
        console.log(`Initial branch text: '${text}'`);

        console.log('Clicking branch button...');
        await branchBtn.click();

        console.log('Clicking New Branch...');
        await page.locator('.new-branch-btn').click();

        console.log('Typing branch name Foo...');
        const input = page.locator('.new-branch-input');
        await input.fill('Foo');
        await input.press('Enter');

        await page.waitForTimeout(1000);
        text = await branchBtn.textContent();
        console.log(`Branch text after creating Foo: '${text}'`);

        console.log('Switching back to Main...');
        await branchBtn.click();
        await page.locator('.branch-item', { hasText: 'Main' }).first().click();

        await page.waitForTimeout(1000);
        text = await branchBtn.textContent();
        console.log(`Branch text after switching to Main: '${text}'`);

        // Let's capture the UI state
        await page.screenshot({ path: '/tmp/webview_check.png' });
        console.log('Screenshot saved to /tmp/webview_check.png');

        process.exit(0);
    } catch (error) {
        console.error('Test failed:', error);
        process.exit(1);
    }
})();
