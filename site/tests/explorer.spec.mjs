// End-to-end: the real built site in a real browser, real clicks. Every test collects V8 JS
// coverage of app.js and viz.js; coverage.mjs fails on any line of them no test ran.
import { test, expect } from '@playwright/test';
import { mkdirSync, writeFileSync } from 'node:fs';

const COV = 'test-results/v8';
let n = 0;
test.beforeEach(async ({ page }) => {
  page.on('pageerror', (e) => { throw e; });
  await page.coverage.startJSCoverage({ resetOnNavigation: false });
});
test.afterEach(async ({ page }) => {
  const entries = (await page.coverage.stopJSCoverage())
    .filter((e) => /\/(app|viz)\.js$/.test(e.url));
  mkdirSync(COV, { recursive: true });
  writeFileSync(`${COV}/${process.pid}-${n++}.json`, JSON.stringify(entries));
});

const ready = (page) => expect(page.locator('body[data-ready="1"]')).toHaveCount(1);
const db = (page) => page.evaluate(() => fetch('data/index.json').then((r) => r.json()));

test('overview: derived stats, charts, gallery, treemap drill and maximize', async ({ page }) => {
  await page.goto('/');
  await ready(page);
  const data = await db(page);
  await expect(page.locator('[data-stat]').first()).toHaveText(
    data.objects.length.toLocaleString('en').replace(/,/g, ' '));
  for (const id of ['tree', 'kinds', 'caps']) {
    await expect(page.locator(`#${id}[data-rendered="1"]`)).toHaveCount(1);
  }
  await expect(page.locator('#gallery img').first()).toBeVisible();
  await expect(page.locator('#footer-meta')).toContainText(data.pack.source_commit.slice(0, 10));
  // Maximize via button, restore with Escape; the state is in the URL.
  await page.click('[data-maximize="tree"]');
  await expect(page).toHaveURL(/max=tree/);
  await page.keyboard.press('Escape');
  await expect(page).not.toHaveURL(/max=tree/);
  await page.click('[data-maximize="kinds"]');
  await page.click('[data-maximize="kinds"]'); // second click restores
  await expect(page).not.toHaveURL(/max=kinds/);
  await page.keyboard.press('Escape'); // nothing maximized: no-op
  // Deep link into a maximized, drilled treemap; click a leaf opens the article.
  await page.goto('/#/?cat=0&max=tree');
  await expect(page.locator('#tree-crumbs nav')).toContainText(data.categories[0].name);
  await expect(page.locator('#tree[data-rendered="1"]')).toHaveCount(1);
  await page.evaluate(() => {
    const c = Viz.instances.get('tree');
    c.trigger('click', { data: { obj: 0 } });
  });
  await expect(page).toHaveURL(/#\/o\/0$/);
  await expect(page.locator('#crumbs')).toContainText(data.objects[0].t);
});

test('charts navigate: kind bar, capability bar, category cell', async ({ page }) => {
  await page.goto('/');
  await ready(page);
  await expect(page.locator('#kinds[data-rendered="1"]')).toHaveCount(1);
  const fire = (id, params) => page.evaluate(([i, p]) => {
    const c = Viz.instances.get(i);
    c.getZr(); // instance exists
    c._$handlers.click.forEach((h) => h.h.call(c, p));
  }, [id, params]);
  await fire('kinds', { name: 'technique' });
  await expect(page).toHaveURL(/explore\?kind=technique/);
  await page.goto('/');
  await ready(page);
  await expect(page.locator('#caps[data-rendered="1"]')).toHaveCount(1);
  await fire('caps', { dataIndex: 0 });
  await expect(page).toHaveURL(/cap=detect/);
  await page.goto('/');
  await expect(page.locator('#tree[data-rendered="1"]')).toHaveCount(1);
  await fire('tree', { data: { cat: 1 } });
  await expect(page).toHaveURL(/cat=1/);
  await expect(page.locator('#tree[data-rendered="1"]')).toHaveCount(1);
  await fire('tree', { data: { obj: 3 } });
  await expect(page).toHaveURL(/#\/o\/3/);
  await expect(page.locator('[data-title]')).toBeVisible();
});

test('explore: search, filters, paging, category select, empty result', async ({ page }) => {
  await page.goto('/');
  await ready(page);
  await page.fill('#topbar-search', 'gaslighting');
  await page.press('#topbar-search', 'Enter');
  await expect(page).toHaveURL(/q=gaslighting/);
  await expect(page.locator('[data-result]').first()).toBeVisible();
  await page.fill('#topbar-search', '');
  await page.press('#topbar-search', 'Enter');
  await expect(page.locator('[data-result]')).toHaveCount(60);
  await page.click('text=Next →');
  await expect(page).toHaveURL(/page=1/);
  await page.click('text=← Previous');
  await page.selectOption('#cat-filter', '2');
  await expect(page).toHaveURL(/cat=2/);
  await page.selectOption('#cat-filter', '');
  await expect(page).not.toHaveURL(/cat=/);
  await page.goto('/#/explore?kind=technique&cap=apply&q=zzzzqqq');
  await expect(page.locator('#results')).toContainText('Nothing matches');
  await page.click('[data-nav="detect"]');
  await expect(page).toHaveURL(/cap=detect/);
});

test('article -> detector -> result, and the neighbourhood tree', async ({ page }) => {
  await page.goto('/');
  await ready(page);
  const data = await db(page);
  const i = data.objects.findIndex((o) => o.caps.includes('detect') && o.caps.includes('compare'));
  await page.goto(`/#/o/${i}`);
  await expect(page.locator('[data-title]')).toHaveText(data.objects[i].t);
  await expect(page.locator('#neighbours[data-rendered="1"]')).toHaveCount(1);
  await page.evaluate(() => {
    const c = Viz.instances.get('neighbours');
    c._$handlers.click.forEach((h) => h.h.call(c, { data: { name: 'group' } }));
  });
  await page.click('[data-run-detector]');
  const marks = data.objects[i].m;
  const order = ['observed', 'not observed', 'unknown', 'not applicable'];
  for (let k = 0; k < marks; k++) {
    await expect(page.locator('[data-marker]')).toBeVisible();
    await page.click(`[data-mark="${order[k % 4]}"]`);
  }
  await expect(page.locator('[data-result-counts]')).toBeVisible();
  await expect(page.locator('[role="note"]')).toContainText('not a score');
  await page.click('text=Start again');
  await expect(page.locator('[data-marker]')).toBeVisible();
  await page.goto(`/#/o/${i}`);
  await page.evaluate(() => {
    const c = Viz.instances.get('neighbours');
    const leaf = c.getOption().series[0].data[0].children[0].children[0];
    c._$handlers.click.forEach((h) => h.h.call(c, { data: leaf }));
  });
  await expect(page).not.toHaveURL(new RegExp(`#/o/${i}$`));
});

test('article with examples and chapters; one without relations', async ({ page }) => {
  await page.goto('/');
  await ready(page);
  const data = await db(page);
  const all = await page.evaluate(async (len) => {
    const out = { ex: -1, bare: -1, ch: -1 };
    for (let i = 0; i < len && (out.ex < 0 || out.bare < 0 || out.ch < 0); i++) {
      const o = await fetch(`data/o/${i}.json`).then((r) => r.json());
      if (out.ex < 0 && o.examples.some((e) => e.benign) && o.examples.some((e) => !e.benign)) out.ex = i;
      if (out.bare < 0 && !o.relations.length && !o.aliases.length) out.bare = i;
      if (out.ch < 0 && o.chapters.length) out.ch = i;
    }
    return out;
  }, data.objects.length);
  await page.goto(`/#/o/${all.ex}`);
  await expect(page.locator('text=Examples from the catalogue')).toBeVisible();
  await page.goto(`/#/o/${all.ch}`);
  await page.locator('details summary').first().click();
  await page.goto(`/#/o/${all.bare}`);
  await expect(page.locator('#neighbours')).toHaveCount(0);
  await page.goto('/#/o/999999');
  await expect(page.locator('#view')).toContainText('Could not load this view');
});

test('quiz: term and trainer, right and wrong, per object, empty pool', async ({ page }) => {
  await page.goto('/');
  await ready(page);
  const quiz = await page.evaluate(() => fetch('data/quiz.json').then((r) => r.json()));
  const t = quiz.findIndex((q) => q.kind === 1 && q.explanation === '' || q.kind === 1);
  await page.goto(`/#/quiz?kind=1&q=${t}`);
  await page.click(`[data-pick="${quiz[t].answer}"]`);
  await expect(page.locator('[data-verdict="correct"]')).toBeVisible();
  await page.click('[data-next]');
  await expect(page.locator('[data-prompt]')).toBeVisible();
  const tr = quiz.findIndex((q) => q.kind === 2 && q.explanation && q.response);
  await page.goto(`/#/quiz?kind=2&q=${tr}`);
  await page.click(`[data-pick="${1 - quiz[tr].answer}"]`);
  await expect(page.locator('[data-verdict="wrong"]')).toBeVisible();
  const benign = quiz.findIndex((q) => q.kind === 2 && q.answer === 1);
  await page.goto(`/#/quiz?kind=2&q=${benign}`);
  await page.click('[data-pick="1"]');
  await expect(page.locator('[data-verdict]')).toContainText('benign');
  const subject = quiz[t].options[quiz[t].answer];
  await page.goto(`/#/o/${subject}`);
  await page.click('text=Take the quiz');
  await expect(page).toHaveURL(new RegExp(`obj=${subject}`));
  await page.click('[data-pick="0"]');
  await page.click('[data-next]');
  await expect(page).toHaveURL(new RegExp(`obj=${subject}`));
  await page.goto('/#/quiz?kind=7');
  await expect(page.locator('#view')).toContainText('No questions here');
});

test('theme toggle persists and re-renders; missing gallery is tolerated', async ({ page }) => {
  await page.goto('/');
  await ready(page);
  const dark = await page.evaluate(() => document.documentElement.classList.contains('dark'));
  await page.click('#theme-toggle');
  expect(await page.evaluate(() => document.documentElement.classList.contains('dark'))).toBe(!dark);
  await page.reload();
  await ready(page);
  expect(await page.evaluate(() => document.documentElement.classList.contains('dark'))).toBe(!dark);
  await page.click('#theme-toggle');
  await page.route('**/data/gallery.json', (r) => r.fulfill({ status: 404, body: '' }));
  await page.goto('/#/explore');
  await page.click('[data-nav="overview"]');
  await expect(page.locator('#gallery img')).toHaveCount(0);
});

test('leaving the overview before the gallery arrives is harmless', async ({ page }) => {
  let release;
  const held = new Promise((r) => { release = r; });
  await page.route('**/data/gallery.json', async (route) => { await held; await route.continue(); });
  await page.goto('/');
  await ready(page);
  await page.click('[data-nav="explore"]');
  await expect(page.locator('[data-result]').first()).toBeVisible();
  release();
  await page.waitForResponse('**/data/gallery.json');
  await page.waitForTimeout(200); // a pageerror here fails the test
});

test('light system theme and reduced motion', async ({ browser }) => {
  const ctx = await browser.newContext({ colorScheme: 'light', reducedMotion: 'reduce' });
  const page = await ctx.newPage();
  await page.coverage.startJSCoverage({ resetOnNavigation: false });
  await page.goto('/');
  await expect(page.locator('body[data-ready="1"]')).toHaveCount(1);
  expect(await page.evaluate(() => document.documentElement.classList.contains('dark'))).toBe(false);
  await expect(page.locator('#kinds[data-rendered="1"]')).toHaveCount(1);
  expect(await page.evaluate(() => Viz.ecInit('does-not-exist'))).toBe(null);
  expect(await page.evaluate(() => { try { Viz.frame('nope', 'a', 'b'); } catch (e) { return e.message; } }))
    .toContain('registry');
  await page.evaluate(() => Viz.maximize('nope', true));
  const entries = (await page.coverage.stopJSCoverage()).filter((e) => /\/(app|viz)\.js$/.test(e.url));
  writeFileSync(`${COV}/${process.pid}-light.json`, JSON.stringify(entries));
  await ctx.close();
});

test('the device: every screen from the atlas, filter, full-size frames, keys, deep link', async ({ page }) => {
  await page.goto('/#/device');
  await ready(page);
  const atlas = await page.evaluate(() => fetch('data/atlas.json').then((r) => r.json()));
  await expect(page.locator('#atlas [data-frame]')).toHaveCount(atlas.frames.length);
  // the filter chips are derived: one per screen with frames, each with its count
  const hubs = atlas.frames.filter((f) => f.screen === 'Hub');
  await page.locator('#screens [data-screen="Hub"]').click();
  await expect(page).toHaveURL(/#\/device\?screen=Hub$/);
  await expect(page.locator('#atlas [data-frame]')).toHaveCount(hubs.length);
  // a frame opens full size; the arrows step and wrap; Escape closes; the URL follows
  await page.locator('#atlas [data-frame]').first().click();
  await expect(page.locator('#lightbox')).toHaveAttribute('data-open', hubs[0].file);
  await expect(page).toHaveURL(new RegExp(`frame=${hubs[0].file}`));
  await page.keyboard.press('ArrowRight');
  await expect(page.locator('#lightbox')).toHaveAttribute('data-open', hubs[1].file);
  await page.keyboard.press('ArrowLeft');
  await page.keyboard.press('ArrowLeft');
  await expect(page.locator('#lightbox')).toHaveAttribute('data-open', hubs[hubs.length - 1].file);
  await page.keyboard.press('Escape');
  await expect(page.locator('#lightbox')).toBeHidden();
  await expect(page).toHaveURL(/#\/device\?screen=Hub$/);
  await page.keyboard.press('ArrowRight'); // nothing open: ignored
  // a deep link to a frame opens it; a click on the backdrop closes it
  await page.goto(`/#/device?frame=${atlas.frames[2].file}`);
  await expect(page.locator('#lightbox')).toHaveAttribute('data-open', atlas.frames[2].file);
  await page.locator('#lightbox').click();
  await expect(page.locator('#lightbox')).toBeHidden();
  // an unknown frame falls back to the first; leaving the page drops the key handler
  await page.goto('/#/device?frame=nope.png');
  await expect(page.locator('#lightbox')).toHaveAttribute('data-open', atlas.frames[0].file);
  await page.evaluate(() => { location.hash = '#/'; }); // leave with the frame still open
  await ready(page);
  await expect(page.locator('#lightbox')).toHaveCount(0);
  await page.keyboard.press('Escape');
  await page.locator('[data-device-link]').click();
  await expect(page.locator('#atlas [data-frame]')).toHaveCount(atlas.frames.length);
  await page.locator('#screens [data-screen=""]').click();
  await expect(page).toHaveURL(/#\/device$/);
});
