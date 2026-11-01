// Drives the remote page against the running app (see remote_e2e.sh).
const { openContext } = await import(process.env.BROWSER_HELPER);
const url = process.env.REMOTE_URL;
const out = process.env.OUT;
let failures = 0;
const check = (ok, what) => {
  console.log(`${ok ? "ok  " : "FAIL"} ${what}`);
  if (!ok) failures++;
};

for (const scheme of ["light", "dark"]) {
  const { page, close } = await openContext({ viewport: { width: 390, height: 844 }, colorScheme: scheme });
  try {
    const errors = [];
    page.on("console", (m) => m.type() === "error" && errors.push(m.text()));
    page.on("pageerror", (e) => errors.push(String(e)));
    await page.goto(url);
    await page.waitForSelector("#ui:not([hidden])", { timeout: 15000 });
    await page.waitForFunction(() => document.getElementById("game").textContent !== "Snes9x Remote");
    check(!page.url().includes("#"), `${scheme}: the key left the address bar`);
    const game = await page.textContent("#game");
    check(game && game !== "No game loaded", `${scheme}: shows the game (${game})`);
    // The memory picture fills in.
    await page.waitForFunction(() => {
      const c = document.getElementById("picture");
      const d = c.getContext("2d").getImageData(0, 0, c.width, Math.min(c.height, 8)).data;
      return c.height > 1 && d.some((v, i) => i % 4 !== 3 && v !== 0);
    }, null, { timeout: 10000 });
    check(true, `${scheme}: the WRAM picture is drawn`);
    const wide = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth);
    check(!wide, `${scheme}: no sideways scrolling at 390 px`);
    await page.screenshot({ path: `${out}/remote-${scheme}.png`, fullPage: true });

    if (scheme === "light") {
      check(await page.isDisabled("#toggle"), "Corrupt is off limits without a recipe");
      // Painting makes a recipe ("Painted"), as in the panel.
      await page.check("#paint");
      await page.fill("#value", "FF");
      const box = await page.locator("#picture").boundingBox();
      await page.mouse.click(box.x + 2, box.y + 2);
      await page.waitForFunction(() => /painted 1 byte of wram with 0xFF/.test(document.getElementById("message").textContent),
        null, { timeout: 5000 });
      check(true, "a tap paints a byte, and the panel's status line names the remote");
      await page.waitForFunction(() => !document.getElementById("toggle").disabled, null, { timeout: 5000 });
      // A starter replaces it (after asking).
      page.once("dialog", (d) => d.accept());
      await page.selectOption("#starter", { label: "Code drip" });
      await page.click("#load-starter");
      await page.waitForFunction(() => /Code drip/.test(document.getElementById("recipe").textContent), null, { timeout: 5000 });
      check((await page.locator("#rules li").count()) > 0, "loading a starter brings its rules");
      await page.click("#toggle");
      await page.waitForFunction(() => document.getElementById("state").textContent === "corrupting", null, { timeout: 5000 });
      check(true, "Corrupt turns corruption on");
      await page.screenshot({ path: `${out}/remote-on.png`, fullPage: true });
      await page.click("#toggle");
      await page.waitForFunction(() => document.getElementById("state").textContent === "off", null, { timeout: 5000 });
      check(true, "Stop turns it off");
    }
    check(errors.length === 0, `${scheme}: no errors in the page${errors.length ? ": " + errors.join("; ") : ""}`);
  } catch (e) {
    check(false, `${scheme}: ${e.message.split("\n")[0]}`);
    await page.screenshot({ path: `${out}/remote-${scheme}-failed.png`, fullPage: true }).catch(() => {});
  } finally {
    await close();
  }
}

// The browser remembers the key: the bare address works the second time.
{
  const { page, close } = await openContext({ viewport: { width: 390, height: 844 } });
  try {
    await page.goto(url);
    await page.waitForSelector("#ui:not([hidden])", { timeout: 15000 });
    await page.goto(url.replace(/#.*/, ""));
    await page.waitForSelector("#ui:not([hidden])", { timeout: 15000 });
    check(true, "the bare address works once the browser has the key");
  } catch (e) {
    check(false, `remembered key: ${e.message.split("\n")[0]}`);
  } finally {
    await close();
  }
}

// A wrong key is refused and says so, and isn't remembered.
{
  const { page, close } = await openContext({ viewport: { width: 390, height: 844 } });
  try {
    await page.goto(url.replace(/#.*/, "#not-the-key"));
    await page.waitForFunction(() => document.getElementById("state").textContent === "refused", null, { timeout: 8000 });
    check(/old/.test(await page.textContent("#recipe")), "a wrong key is refused, with a reason");
    check((await page.evaluate(() => localStorage.getItem("s9x-remote-key"))) === null, "and forgotten");
  } catch (e) {
    check(false, `wrong key: ${e.message.split("\n")[0]}`);
  } finally {
    await close();
  }
}
process.exit(failures ? 1 : 0);
