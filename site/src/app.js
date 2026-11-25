/* Prismatic Pocket explorer: every view is derived from data/index.json, data/o/<n>.json and
 * data/quiz.json, which tools/explorer.py builds from the same IR as the device pack. Object
 * <n> here is object <n> on the Flipper. Routes live in the hash, so every view is a link. */
(function () {
  'use strict';

  const PAGE = 60;
  const ROLES = [
    ['indicators', 'How to recognise it'],
    ['actions', 'What to do'],
    ['escalation', 'When to escalate'],
    ['alternatives', "Don't confuse with"],
    ['validation', 'Validation tests'],
  ];
  const REL = { confused_with: 'Confusable with', related_to: 'Related' };
  const view = document.getElementById('view');
  let DB = null;
  let QUIZ = null;
  const cache = new Map();

  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const fold = (s) => String(s || '').normalize('NFKD').replace(/[̀-ͯ]/g, '').toLowerCase();
  const fmt = (n) => Number(n).toLocaleString('en').replace(/,/g, ' ');

  async function json(url) {
    if (!cache.has(url)) cache.set(url, fetch(url).then((r) => { if (!r.ok) throw new Error(url + ' ' + r.status); return r.json(); }));
    return cache.get(url);
  }
  const object = (i) => json(`data/o/${i}.json`);

  function route() {
    const [path, query] = (location.hash.slice(1) || '/').split('?');
    return { parts: path.split('/').filter(Boolean), params: new URLSearchParams(query || '') };
  }
  function go(path, params) {
    const q = params && params.toString();
    location.hash = path + (q ? '?' + q : '');
  }

  function badge(text, tone) {
    const tones = {
      blue: 'bg-blue-100 text-blue-800 dark:bg-blue-900 dark:text-blue-300',
      orange: 'bg-orange-100 text-orange-800 dark:bg-orange-900 dark:text-orange-300',
      green: 'bg-green-100 text-green-800 dark:bg-green-900 dark:text-green-300',
      gray: 'bg-gray-100 text-gray-800 dark:bg-gray-700 dark:text-gray-300',
      purple: 'bg-purple-100 text-purple-800 dark:bg-purple-900 dark:text-purple-300',
    };
    return `<span class="text-xs font-medium mr-1 px-2.5 py-0.5 rounded-md ${tones[tone] || tones.gray}">${esc(text)}</span>`;
  }
  const CAP_TONE = { detect: 'orange', apply: 'green', compare: 'purple', quiz: 'blue', browse: 'gray', assess: 'purple' };
  const capBadges = (caps) => caps.filter((c) => c !== 'browse').map((c) => badge(c, CAP_TONE[c])).join('');

  /* The page header's Pro breadcrumb (static nav#crumbs); returns '' so views can inline it. */
  function breadcrumb(items) {
    const li = items.map((it, n) => n === items.length - 1
      ? `<li aria-current="page"><div class="flex items-center">${n ? '<span class="mx-1 text-gray-400">/</span>' : ''}<span class="ml-1 text-sm font-medium text-gray-500 md:ml-2 dark:text-gray-400">${esc(it.label)}</span></div></li>`
      : `<li><div class="flex items-center">${n ? '<span class="mx-1 text-gray-400">/</span>' : ''}<a href="${it.href}" class="ml-1 text-sm font-medium text-gray-700 hover:text-primary-600 md:ml-2 dark:text-gray-300 dark:hover:text-white">${esc(it.label)}</a></div></li>`).join('');
    return `<ol class="inline-flex items-center space-x-1 md:space-x-2">${li}</ol>`;
  }
  const crumbs = (items) => { document.getElementById('crumbs').innerHTML = breadcrumb(items); return ''; };

  function card(inner, extra = '') {
    return `<section class="p-4 bg-white rounded-lg shadow sm:p-6 dark:bg-gray-800 ${extra}">${inner}</section>`;
  }

  function markActive(key) {
    document.querySelectorAll('[data-nav]').forEach((a) => {
      const on = a.getAttribute('data-nav') === key;
      a.classList.toggle('bg-gray-100', on);
      a.classList.toggle('dark:bg-gray-700', on);
    });
  }

  /* ------------------------------------------------------------------ overview */

  function stat(label, value, href) {
    return `<a href="${href}" class="block p-4 bg-white rounded-lg shadow sm:p-6 dark:bg-gray-800 hover:ring-2 hover:ring-primary-500">
      <span class="text-2xl font-bold leading-none text-gray-900 sm:text-3xl dark:text-white" data-stat>${fmt(value)}</span>
      <h3 class="text-base font-normal text-gray-500 dark:text-gray-400">${esc(label)}</h3></a>`;
  }

  function overview() {
    markActive('overview');
    crumbs([{ label: 'Overview' }]);
    const quiz1 = QUIZ.filter((q) => q.kind === 1).length;
    const quiz2 = QUIZ.length - quiz1;
    view.innerHTML = `
      <div class="grid gap-4 xl:grid-cols-3">
        ${card(`
          <h1 class="text-2xl font-bold text-gray-900 sm:text-3xl dark:text-white">The Kompendium, in your pocket</h1>
          <div class="mt-3 text-gray-600 dark:text-gray-300 prose-pocket">
            <p>Manipulation rarely announces itself. It arrives as a deadline you cannot verify, a choice narrowed to two bad options,
            a memory that is questioned until you stop trusting it. The <a class="text-primary-600 dark:text-primary-400 hover:underline" href="https://github.com/korczis/kompendium-epistemicke-anihilace">Kompendium</a>
            describes how the ability to know things is undermined, and how to defend it.</p>
            <p><b>Prismatic Pocket</b> compiles every page of it into a knowledge pack that runs offline on a Flipper Zero: an encyclopedia,
            a detector that walks you through observable markers, the book's guidance on what to do, and a trainer. This site is the same pack,
            in a browser. Pick anything below; every number and every link is derived from the pack.</p>
            <p class="text-sm text-gray-500 dark:text-gray-400">The detector counts what you mark. It never scores, ranks or diagnoses anyone.</p>
          </div>`, 'xl:col-span-2')}
        ${card(`
          <h3 class="text-xl font-bold leading-none text-gray-900 dark:text-white mb-3">On the device</h3>
          <div class="grid grid-cols-2 gap-2" id="gallery"></div>
          <a href="#/device" class="inline-block mt-3 text-sm text-primary-600 dark:text-primary-400 hover:underline" data-device-link>Every screen of the app →</a>`)}
      </div>
      <div class="grid gap-4 mt-4 sm:grid-cols-2 xl:grid-cols-5">
        ${stat('knowledge objects', DB.objects.length, '#/explore')}
        ${stat('categories', DB.categories.length, '#/explore')}
        ${stat('detectors', DB.capCounts.detect, '#/explore?cap=detect')}
        ${stat('typed links', DB.relations, '#/explore')}
        ${stat('quiz questions', QUIZ.length, '#/quiz?kind=1')}
      </div>
      <div class="grid gap-4 mt-4 sm:grid-cols-2 xl:grid-cols-4" data-tools>
        ${stat('assessments to grade with', DB.capCounts.assess || 0, '#/explore?cap=assess')}
        ${stat('scorecards to answer', DB.capCounts.score || 0, '#/explore?cap=score')}
        ${stat('crisis simulations to run', DB.capCounts.simulate || 0, '#/explore?cap=simulate')}
        ${stat('calculators to drive', DB.kindCounts.algorithm || 0, '#/explore?kind=algorithm')}
      </div>
      <div class="grid gap-4 mt-4 xl:grid-cols-2">
        ${Viz.frame('tree', 'The whole pack', 'Categories sized by objects; click a category to open it, an object to read it')}
        <div class="grid gap-4">
          ${Viz.frame('kinds', 'Object kinds', 'Click a bar to list that kind')}
          ${Viz.frame('caps', 'What you can do with it', `Click a bar to list those objects · quiz: ${fmt(quiz1)} term + ${fmt(quiz2)} example questions`)}
        </div>
      </div>`;
    fetch('data/gallery.json').then((r) => r.ok ? r.json() : null).then((g) => {
      const box = document.getElementById('gallery'); // gone if the reader already left
      if (!g || !box) return;
      box.innerHTML = g.frames.slice(0, 6).map((f) =>
        `<img class="lcd rounded border border-gray-200 dark:border-gray-700" src="screens/${esc(f.file)}" alt="${esc(f.caption)}" title="${esc(f.caption)}" loading="lazy">`).join('');
    });
    Viz.afterPaint(() => { renderKinds(); renderCaps(); renderTree(); });
  }

  /* ------------------------------------------------------------------ the device */

  /* Every screen of the app, from the screen atlas (docs/evidence/atlas, rendered pixel-exact by
   * the host simulator and checked current in CI): filter by screen, open a frame full-viewport,
   * step with the arrow keys. The filter and the open frame live in the URL. */
  async function device(params) {
    markActive('device');
    crumbs([{ label: 'Overview', href: '#/' }, { label: 'The device' }]);
    const atlas = await json('data/atlas.json');
    const screen = params.get('screen') || '';
    const shown = atlas.frames.filter((f) => !screen || f.screen === screen);
    const counts = new Map(atlas.screens.map((s) => [s, atlas.frames.filter((f) => f.screen === s).length]));
    const chip = (label, value, n) => {
      const on = value === screen;
      const np = new URLSearchParams(value ? { screen: value } : {});
      return `<a href="#/device${value ? '?' + np : ''}" data-screen="${esc(value)}" class="px-3 py-1 text-sm rounded-lg ${on ? 'bg-primary-700 text-white' : 'bg-gray-100 text-gray-800 hover:bg-primary-100 dark:bg-gray-700 dark:text-gray-200'}">${esc(label)} <span class="opacity-70">${n}</span></a>`;
    };
    const keys = (k) => k ? k.split(',').map((x) => `<kbd class="px-1.5 py-0.5 text-xs font-semibold text-gray-800 bg-gray-100 border border-gray-200 rounded dark:bg-gray-600 dark:text-gray-100 dark:border-gray-500">${esc(x)}</kbd>`).join(' ') : '<span class="text-xs text-gray-500">start</span>';
    view.innerHTML = `
      ${card(`
        <h1 class="text-2xl font-bold text-gray-900 sm:text-3xl dark:text-white">Every screen of the app</h1>
        <p class="mt-2 text-gray-600 dark:text-gray-300">${fmt(atlas.frames.length)} frames of ${fmt(atlas.screens.length)} screens on a 128 x 64 display, drawn by the firmware's own renderer from this pack.
        Under each frame are the keys that reach it from Home. Click a frame to see it full size; the arrow keys step through them and Escape closes.</p>
        <div class="flex flex-wrap gap-2 mt-4" id="screens">${chip('All', '', atlas.frames.length)}${atlas.screens.filter((s) => counts.get(s)).map((s) => chip(s, s, counts.get(s))).join('')}</div>`)}
      <div class="grid gap-4 mt-4 sm:grid-cols-2 xl:grid-cols-4" id="atlas">
        ${shown.map((f) => `<button type="button" data-frame="${esc(f.file)}" class="text-left p-3 bg-white rounded-lg shadow hover:ring-2 hover:ring-primary-500 dark:bg-gray-800">
          <img class="lcd w-full rounded" src="atlas/${esc(f.file)}" alt="${esc(f.caption)}" loading="lazy">
          <div class="mt-2">${badge(f.screen, 'blue')}</div>
          <p class="mt-1 text-sm text-gray-700 dark:text-gray-300">${esc(f.caption)}</p>
          <p class="mt-1 leading-6">${keys(f.keys)}</p></button>`).join('')}
      </div>
      <div id="lightbox" class="hidden fixed inset-0 z-50 bg-gray-900/95 flex flex-col items-center justify-center p-4" role="dialog" aria-modal="true" aria-label="Frame">
        <img id="lightbox-img" class="lcd w-full max-w-5xl" alt="">
        <p id="lightbox-caption" class="mt-4 text-center text-gray-100"></p>
        <p class="mt-2 text-sm text-gray-400">Left / Right: previous / next · Escape: close</p>
      </div>`;
    const box = document.getElementById('lightbox');
    const open = (file) => {
      const at = shown.findIndex((f) => f.file === file);
      const f = shown[at < 0 ? 0 : at];
      document.getElementById('lightbox-img').src = 'atlas/' + f.file;
      document.getElementById('lightbox-img').alt = f.caption;
      document.getElementById('lightbox-caption').textContent = `${f.screen} · ${f.caption}`;
      box.classList.remove('hidden');
      box.setAttribute('data-open', f.file);
    };
    const setFrame = (file) => {
      const np = new URLSearchParams(params);
      if (file) np.set('frame', file); else np.delete('frame');
      history.replaceState(null, '', '#/device' + (np.toString() ? '?' + np : ''));
      if (file) open(file); else { box.classList.add('hidden'); box.removeAttribute('data-open'); }
    };
    view.querySelectorAll('[data-frame]').forEach((b) => b.addEventListener('click', () => setFrame(b.getAttribute('data-frame'))));
    box.addEventListener('click', () => setFrame(''));
    const onKey = (e) => {
      if (!document.body.contains(box)) { document.removeEventListener('keydown', onKey); return; }
      const cur = box.getAttribute('data-open');
      if (!cur) return;
      const at = shown.findIndex((f) => f.file === cur);
      if (e.key === 'Escape') setFrame('');
      else if (e.key === 'ArrowRight') setFrame(shown[(at + 1) % shown.length].file);
      else if (e.key === 'ArrowLeft') setFrame(shown[(at - 1 + shown.length) % shown.length].file);
    };
    document.addEventListener('keydown', onKey);
    if (params.get('frame')) open(params.get('frame'));
  }

  function barOption(names, values, t) {
    return {
      grid: { left: 8, right: 24, top: 8, bottom: 8, containLabel: true },
      tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
      xAxis: { type: 'value', axisLabel: { color: t.muted }, splitLine: { lineStyle: { color: t.grid } } },
      yAxis: { type: 'category', data: names, inverse: true, axisLabel: { color: t.ink } },
      series: [{ type: 'bar', data: values, label: { show: true, position: 'right', color: t.muted },
        itemStyle: { borderRadius: [0, 4, 4, 0] } }],
    };
  }

  function renderKinds() {
    const chart = Viz.ecInit('kinds');
    if (!chart) return;
    const entries = Object.entries(DB.kindCounts).sort((a, b) => b[1] - a[1]);
    const t = Viz.theme();
    const opt = barOption(entries.map((e) => e[0]), entries.map((e) => e[1]), t);
    opt.series[0].colorBy = 'data';
    chart.setOption(opt);
    chart.on('click', (p) => go('/explore', new URLSearchParams({ kind: p.name })));
  }

  function renderCaps() {
    const chart = Viz.ecInit('caps');
    if (!chart) return;
    const labels = { detect: 'detector', apply: 'what to do', compare: 'confusables', quiz: 'quiz' };
    const keys = Object.keys(labels);
    const opt = barOption(keys.map((k) => labels[k]), keys.map((k) => DB.capCounts[k]), Viz.theme());
    opt.series[0].colorBy = 'data';
    chart.setOption(opt);
    chart.on('click', (p) => go('/explore', new URLSearchParams({ cap: keys[p.dataIndex] })));
  }

  /* Treemap with our own drill: the root shows categories; a category re-roots to its objects. */
  function renderTree() {
    const chart = Viz.ecInit('tree');
    if (!chart) return;
    const at = route().params.get('cat');
    const t = Viz.theme();
    let data;
    if (at === null) {
      data = DB.categories.map((c, n) => ({ name: c.name, value: c.count, cat: n }));
    } else {
      const c = DB.categories[+at];
      data = DB.objects.slice(c.first, c.first + c.count).map((o, k) => ({ name: o.t, value: 1 + o.m, obj: c.first + k }));
    }
    document.getElementById('tree-crumbs').innerHTML = at === null ? '' :
      `<nav class="flex mb-2" aria-label="Treemap path">${breadcrumb([{ label: 'All categories', href: '#/' }, { label: DB.categories[+at].name }])}</nav>`;
    chart.setOption({
      tooltip: { formatter: (p) => `${esc(p.name)}${p.data.cat !== undefined ? ` · ${p.value} objects` : ''}` },
      series: [{
        type: 'treemap', data, nodeClick: false, breadcrumb: { show: false }, roam: false,
        label: { show: true, overflow: 'truncate', ellipsis: '…', color: '#fff' },
        upperLabel: { show: false },
        itemStyle: { borderColor: t.surface, borderWidth: 2, gapWidth: 2 },
        colorMappingBy: 'index',
      }],
    }, true);
    chart.off('click');
    chart.on('click', (p) => {
      if (p.data.cat !== undefined) go('/', new URLSearchParams({ cat: p.data.cat }));
      else if (p.data.obj !== undefined) go('/o/' + p.data.obj);
    });
  }

  /* ------------------------------------------------------------------ explore */

  function matches(o, i, params) {
    const q = fold(params.get('q'));
    if (params.get('cat') !== null && o.c !== +params.get('cat')) return false;
    if (params.get('kind') && DB.kinds[o.k] !== params.get('kind')) return false;
    if (params.get('cap') && !o.caps.includes(params.get('cap'))) return false;
    if (q) {
      const hay = fold([o.t, o.code, ...(o.a || [])].join(' '));
      return q.split(/\s+/).every((w) => hay.includes(w));
    }
    return true;
  }

  function explore(params) {
    const nav = params.get('kind') === 'algorithm' ? 'calculate' : params.get('cap');
    markActive(['detect', 'apply', 'assess', 'score', 'simulate', 'calculate'].includes(nav) ? nav : 'explore');
    const hits = [];
    DB.objects.forEach((o, i) => { if (matches(o, i, params)) hits.push(i); });
    const page = Math.max(0, +(params.get('page') || 0));
    const pages = Math.max(1, Math.ceil(hits.length / PAGE));
    const shown = hits.slice(page * PAGE, page * PAGE + PAGE);
    const filters = [];
    if (params.get('q')) filters.push(`“${params.get('q')}”`);
    if (params.get('cat') !== null) filters.push(DB.categories[+params.get('cat')].name);
    if (params.get('kind')) filters.push(params.get('kind'));
    if (params.get('cap')) filters.push(params.get('cap'));
    const catOptions = DB.categories.map((c, n) => `<option value="${n}" ${String(n) === params.get('cat') ? 'selected' : ''}>${esc(c.name)} (${c.count})</option>`).join('');
    const pageLink = (p) => { const np = new URLSearchParams(params); np.set('page', p); return '#/explore?' + np; };
    view.innerHTML = `
      ${crumbs([{ label: 'Overview', href: '#/' }, { label: 'Explore' }])}
      ${card(`
        <div class="flex flex-wrap gap-3 items-end justify-between">
          <div>
            <h1 class="text-xl font-semibold text-gray-900 sm:text-2xl dark:text-white">${fmt(hits.length)} objects${filters.length ? ' · ' + esc(filters.join(' · ')) : ''}</h1>
            <p class="text-sm text-gray-500 dark:text-gray-400">Page ${page + 1} of ${pages}</p>
          </div>
          <div class="flex flex-wrap gap-2">
            <select id="cat-filter" class="bg-gray-50 border border-gray-300 text-gray-900 text-sm rounded-lg focus:ring-primary-500 focus:border-primary-500 p-2.5 dark:bg-gray-700 dark:border-gray-600 dark:text-white">
              <option value="">All categories</option>${catOptions}</select>
            <a href="#/explore" class="text-white bg-primary-700 hover:bg-primary-800 focus:ring-4 focus:ring-primary-300 font-medium rounded-lg text-sm px-4 py-2.5 dark:bg-primary-600 dark:hover:bg-primary-700">Clear filters</a>
          </div>
        </div>`)}
      <div class="mt-4 bg-white rounded-lg shadow dark:bg-gray-800 divide-y divide-gray-200 dark:divide-gray-700" id="results">
        ${shown.map((i) => { const o = DB.objects[i]; return `
          <a href="#/o/${i}" class="flex flex-wrap items-center justify-between gap-2 p-4 hover:bg-gray-100 dark:hover:bg-gray-700" data-result="${i}">
            <div><div class="text-base font-semibold text-gray-900 dark:text-white">${esc(o.t)}</div>
            <div class="text-sm text-gray-500 dark:text-gray-400">${esc(DB.kinds[o.k])}${o.code ? ' · ' + esc(o.code) : ''} · ${esc(DB.categories[o.c].name)}</div></div>
            <div>${capBadges(o.caps)}</div></a>`; }).join('') || '<p class="p-4 text-gray-500 dark:text-gray-400">Nothing matches.</p>'}
      </div>
      <div class="flex justify-between mt-4">
        ${page > 0 ? `<a href="${pageLink(page - 1)}" class="px-4 py-2 text-sm font-medium text-gray-900 bg-white border border-gray-200 rounded-lg hover:bg-gray-100 dark:bg-gray-800 dark:text-gray-400 dark:border-gray-700">← Previous</a>` : '<span></span>'}
        ${page + 1 < pages ? `<a href="${pageLink(page + 1)}" class="px-4 py-2 text-sm font-medium text-gray-900 bg-white border border-gray-200 rounded-lg hover:bg-gray-100 dark:bg-gray-800 dark:text-gray-400 dark:border-gray-700">Next →</a>` : ''}
      </div>`;
    document.getElementById('cat-filter').addEventListener('change', (e) => {
      const np = new URLSearchParams(params);
      np.delete('page');
      if (e.target.value === '') np.delete('cat'); else np.set('cat', e.target.value);
      go('/explore', np);
    });
  }

  /* ------------------------------------------------------------------ article */

  /* An assessment, interactive: one grade per scale, the code built as you choose, every
   * choice in the URL (?g=<level>,<level>), so a graded claim is a link. Nothing is computed:
   * the reader chooses, the page records and explains. */
  function assessCard(o, params) {
    const picks = (params.get('g') || '').split(',').map((x) => (x === '' ? -1 : Number(x)));
    const chosen = o.rubric.map((axis, a) => axis.levels[picks[a]]);
    const done = chosen.every(Boolean);
    const scales = o.rubric.map((axis, a) => `
      <h4 class="mt-4 mb-2 text-sm font-semibold text-gray-500 uppercase dark:text-gray-400">${esc(axis.name)}</h4>
      <div class="grid gap-2 sm:grid-cols-2">${axis.levels.map(([code, label, meaning], l) => {
        const on = picks[a] === l;
        return `<button type="button" data-act="g:${a}:${l}" data-grade="${a}:${l}" aria-pressed="${on}" class="text-left p-3 rounded-lg border ${on ? 'border-primary-600 bg-primary-50 dark:bg-gray-700' : 'border-gray-200 hover:bg-gray-100 dark:border-gray-700 dark:hover:bg-gray-700'}">
          <span class="font-bold text-gray-900 dark:text-white">${esc(code)}</span> <span class="text-gray-800 dark:text-gray-200">${esc(label)}</span>
          ${meaning ? `<span class="block mt-1 text-sm text-gray-500 dark:text-gray-400">${esc(meaning)}</span>` : ''}</button>`;
      }).join('')}</div>`).join('');
    const result = done ? `
      <div class="mt-5 p-4 rounded-lg bg-gray-50 dark:bg-gray-900" data-grade-result>
        <span class="text-3xl font-bold text-gray-900 dark:text-white" data-grade-code>${esc(chosen.map((x) => x[0]).join(''))}</span>
        <span class="ml-2 text-gray-500 dark:text-gray-400">${esc(o.code)}</span>
        <ul class="mt-2 text-sm text-gray-700 dark:text-gray-300">${chosen.map((x, a) => `<li><b>${esc(o.rubric[a].name)}:</b> ${esc(x[0])} ${esc(x[1])}${x[2] ? ` (${esc(x[2])})` : ''}</li>`).join('')}</ul>
      </div>` : '<p class="mt-4 text-sm text-gray-500 dark:text-gray-400" data-grade-result>Choose a grade on every scale.</p>';
    return `<h3 class="text-xl font-bold text-gray-900 dark:text-white">Assess</h3>${scales}${result}`;
  }

  /* ------------------------------------------------------------------ interactive tools */
  /* Scorecards, simulations and calculators, as on the device: the same rules ported from the
   * Prismatic Platform, the state in the URL so every run is a link. Each tool is a section
   * whose buttons carry data-act="<tool>:<arg>"; one handler applies the act to the URL and
   * re-renders the section. Nothing here judges anyone: the reader chooses, the page explains. */

  const btn = (act, label, on, extra = '') =>
    `<button type="button" data-act="${act}" aria-pressed="${!!on}" class="text-left px-3 py-2 rounded-lg border ${on ? 'border-primary-600 bg-primary-50 dark:bg-gray-700' : 'border-gray-200 hover:bg-gray-100 dark:border-gray-700 dark:hover:bg-gray-700'} ${extra}">${label}</button>`;
  const list = (p, key) => (p.get(key) || '').split(',').filter((x) => x !== '').map(Number);
  const meter = (value, max, label) => `<div class="w-full h-3 bg-gray-200 rounded-full dark:bg-gray-700" role="img" aria-label="${esc(label)}">
      <div class="h-3 rounded-full bg-primary-600" style="width:${Math.max(0, Math.min(100, (100 * value) / max))}%"></div></div>`;

  /* Scorecard: base + the weights chosen, clamped; the band is the first whose top it does not exceed. */
  function scoreCard(o, p) {
    const c = o.scorecard;
    const picks = list(p, 's');
    const chosen = c.questions.map((q, n) => q.choices[picks[n]]);
    const total = Math.max(c.min, Math.min(c.max, c.base + chosen.reduce((s, x) => s + (x ? x[1] : 0), 0)));
    const band = c.bands.find((b) => total <= b[0]);
    const qs = c.questions.map((q, n) => `
      <p class="mt-4 mb-2 font-medium text-gray-900 dark:text-white">${n + 1}. ${esc(q.text)}</p>
      <div class="flex flex-wrap gap-2">${q.choices.map((ch, k) => btn(`score:${n}:${k}`,
        `${esc(ch[0])} <span class="text-gray-500 dark:text-gray-400">${ch[1] > 0 ? '+' : ''}${ch[1]}</span>`, picks[n] === k)).join('')}</div>`).join('');
    const notes = chosen.filter((x) => x && x[2]).map((x) => `<li>${esc(x[2])}</li>`).join('');
    return `<h3 class="text-xl font-bold text-gray-900 dark:text-white">Score it</h3>
      <div class="mt-3 p-4 rounded-lg bg-gray-50 dark:bg-gray-900" data-score-result>
        <span class="text-3xl font-bold text-gray-900 dark:text-white" data-score>${total}</span>
        <span class="ml-2 font-semibold text-gray-700 dark:text-gray-300" data-band>${esc(band[1])} ${esc(band[2])}</span>
        <div class="mt-2">${meter(total - c.min, c.max - c.min, `score ${total}`)}</div>
        ${band[3] ? `<p class="mt-2 text-sm text-gray-600 dark:text-gray-400">${esc(band[3])}</p>` : ''}
        ${notes ? `<ul class="mt-2 text-sm list-disc list-inside text-orange-700 dark:text-orange-400" data-flags>${notes}</ul>` : ''}
      </div>${qs}`;
  }

  /* Simulation: trust from the start, clamped 0..100 per decision; the outcome by the source's rules. */
  function simState(sim, picks) {
    let trust = sim.start_trust;
    let pos = 0;
    let neg = 0;
    picks.forEach((k, n) => {
      const c = sim.steps[n].options[k].consequence;
      trust = Math.max(0, Math.min(100, trust + c.trust_delta));
      if (c.impact === 'positive') pos++;
      if (c.impact === 'negative' || c.impact === 'critical') neg++;
    });
    const outcome = trust >= 70 && pos > neg ? 'Optimal' : trust >= 40 && pos >= neg ? 'Acceptable'
      : trust < 20 ? 'Critical failure' : 'Suboptimal';
    return { trust, pos, neg, outcome };
  }

  function simCard(o, p) {
    const sim = o.simulation;
    const picks = list(p, 'sim').slice(0, sim.steps.length);
    const st = simState(sim, picks);
    const done = picks.length === sim.steps.length;
    const history = picks.map((k, n) => {
      const op = sim.steps[n].options[k];
      return `<li class="mb-3"><p class="text-sm text-gray-500 dark:text-gray-400">Step ${n + 1}: ${esc(sim.steps[n].prompt)}</p>
        <p class="font-medium text-gray-900 dark:text-white">${esc(op.text)}</p>
        <p class="text-sm text-gray-700 dark:text-gray-300">${badge(op.consequence.type, op.consequence.trust_delta >= 0 ? 'green' : 'orange')} ${esc(op.consequence.text)}
          <b>${op.consequence.trust_delta >= 0 ? '+' : ''}${op.consequence.trust_delta}</b></p></li>`;
    }).join('');
    const step = done ? '' : `<p class="mt-4 font-medium text-gray-900 dark:text-white" data-step>Step ${picks.length + 1} of ${sim.steps.length}: ${esc(sim.steps[picks.length].prompt)}</p>
      <div class="grid gap-2 mt-2">${sim.steps[picks.length].options.map((op, k) => btn(`sim:${k}`,
        `${esc(op.text)} <span class="text-sm text-gray-500 dark:text-gray-400">(${esc(op.risk)} risk)</span>`, false)).join('')}</div>`;
    const outcome = done ? `<div class="mt-4 p-4 rounded-lg bg-gray-50 dark:bg-gray-900">
        <span class="text-2xl font-bold text-gray-900 dark:text-white" data-outcome>${esc(st.outcome)}</span>
        <p class="mt-1 text-sm text-gray-600 dark:text-gray-400">Trust ${st.trust}. ${st.pos} positive and ${st.neg} negative decisions of ${picks.length}.</p></div>` : '';
    return `<h3 class="text-xl font-bold text-gray-900 dark:text-white">Run the simulation</h3>
      <div class="flex items-center gap-3 mt-3"><span class="text-sm text-gray-500 dark:text-gray-400">Trust</span>
        <span class="font-bold text-gray-900 dark:text-white" data-trust>${st.trust}</span><div class="flex-1">${meter(st.trust, 100, `trust ${st.trust}`)}</div></div>
      ${history ? `<ol class="mt-4">${history}</ol>` : ''}${step}${outcome}
      <div class="flex gap-2 mt-4">${picks.length ? btn('sim:undo', 'Take back the last decision') + btn('sim:reset', 'Start over') : ''}</div>
      <p class="mt-3 text-xs text-gray-500 dark:text-gray-400">A training exercise from the Prismatic Platform, not guidance for a real emergency. In an emergency, call 112.</p>`;
  }

  /* Calculator: Bayesian fusion of independent sources, one at a time, from the prior. */
  function bayesFuse(prior, sources) {
    return sources.reduce((pr, c) => {
      const den = c * pr + (1 - c) * (1 - pr);
      return den > 0 ? (c * pr) / den : pr;
    }, prior);
  }

  function calcCard(o, p) {
    const c = o.calculator;
    const prior = p.has('prior') ? Number(p.get('prior')) : c.prior;
    const src = list(p, 'src');
    const belief = 100 * bayesFuse(prior / 100, src.map((x) => x / 100));
    const status = belief >= c.confirm ? 'confirmed' : belief <= c.reject ? 'rejected' : 'tentative';
    return `<h3 class="text-xl font-bold text-gray-900 dark:text-white">Calculate</h3>
      <div class="mt-3 p-4 rounded-lg bg-gray-50 dark:bg-gray-900">
        <span class="text-3xl font-bold text-gray-900 dark:text-white" data-belief>${belief.toFixed(1)}%</span>
        <span class="ml-2 font-semibold text-gray-700 dark:text-gray-300" data-status>${status}</span>
        <div class="mt-2">${meter(belief, 100, `belief ${belief.toFixed(1)}%`)}</div></div>
      <p class="mt-4 mb-2 text-sm text-gray-600 dark:text-gray-400">Prior <b data-prior>${prior}%</b></p>
      <div class="flex gap-2">${btn('calc:prior:-10', 'Prior -10')}${btn('calc:prior:10', 'Prior +10')}</div>
      <p class="mt-4 mb-2 text-sm text-gray-600 dark:text-gray-400">Add an independent source at a confidence</p>
      <div class="flex flex-wrap gap-2">${c.choices.map((x) => btn(`calc:add:${x}`, `${x}%`)).join('')}</div>
      <p class="mt-4 mb-2 text-sm text-gray-600 dark:text-gray-400">Sources so far (click one to remove it)</p>
      <div class="flex flex-wrap gap-2" data-sources>${src.length ? src.map((x, k) => btn(`calc:rm:${k}`, `${x}% ×`)).join('') : '<span class="text-sm text-gray-500">none yet</span>'}</div>
      <div class="mt-4">${btn('calc:reset', 'Start over')}</div>`;
  }

  /* Each act changes the URL parameters only; the section re-renders from them. */
  const ACTS = {
    g(o, p, [a, l]) {
      const picks = (p.get('g') || '').split(',');
      while (picks.length < o.rubric.length) picks.push('');
      picks[a] = String(l);
      p.set('g', picks.join(','));
    },
    score(o, p, [n, k]) {
      const picks = (p.get('s') || '').split(',');
      while (picks.length < o.scorecard.questions.length) picks.push('');
      picks[n] = String(k);
      p.set('s', picks.join(','));
    },
    sim(o, p, [what]) {
      const picks = list(p, 'sim');
      if (what === 'reset') picks.length = 0;
      else if (what === 'undo') picks.pop();
      else picks.push(Number(what));
      if (picks.length) p.set('sim', picks.join(',')); else p.delete('sim');
    },
    calc(o, p, [what, arg]) {
      const src = list(p, 'src');
      const prior = p.has('prior') ? Number(p.get('prior')) : o.calculator.prior;
      if (what === 'prior') p.set('prior', String(Math.max(10, Math.min(90, prior + Number(arg)))));
      if (what === 'add') src.push(Number(arg));
      if (what === 'rm') src.splice(Number(arg), 1);
      if (what === 'reset') { src.length = 0; p.delete('prior'); }
      if (src.length) p.set('src', src.join(',')); else p.delete('src');
    },
  };

  const TOOLS = [
    { field: 'rubric', id: 'assess', render: (o, p) => assessCard(o, p) },
    { field: 'scorecard', id: 'score', render: scoreCard },
    { field: 'simulation', id: 'simulate', render: simCard },
    { field: 'calculator', id: 'calculate', render: calcCard },
  ];
  const present = (o, t) => (Array.isArray(o[t.field]) ? o[t.field].length : !!o[t.field]);

  function toolSections(o, p) {
    return TOOLS.filter((t) => present(o, t)).map((t) =>
      `<section id="${t.id}" class="p-4 bg-white rounded-lg shadow sm:p-6 dark:bg-gray-800">${t.render(o, p)}</section>`).join('');
  }

  function bindTools(i, o) {
    TOOLS.filter((t) => present(o, t)).forEach((t) => {
      const box = document.getElementById(t.id);
      box.addEventListener('click', (e) => {
        const b = e.target.closest('[data-act]');
        if (!b) return;
        const [tool, ...args] = b.getAttribute('data-act').split(':');
        const p = route().params;
        ACTS[tool](o, p, args);
        history.replaceState(null, '', `#/o/${i}${p.toString() ? '?' + p : ''}`);
        box.innerHTML = t.render(o, p);
      });
    });
  }

  async function article(i) {
    markActive('explore');
    const o = await object(i);
    const cat = DB.categories[DB.objects[i].c];
    const catIdx = DB.objects[i].c;
    const list = (items) => `<ul class="space-y-2 text-gray-700 dark:text-gray-300 list-disc list-inside">${items.map((x) => `<li>${esc(x)}</li>`).join('')}</ul>`;
    const roles = ROLES.filter(([k]) => o[k] && o[k].length).map(([k, title]) => card(`
      <h3 class="text-xl font-bold text-gray-900 dark:text-white mb-3">${title}</h3>${list(o[k])}
      ${k === 'indicators' ? `<a href="#/detect/${i}" class="inline-block mt-4 text-white bg-orange-600 hover:bg-orange-700 focus:ring-4 focus:ring-orange-300 font-medium rounded-lg text-sm px-4 py-2.5" data-run-detector>Run the detector (${o.indicators.length} markers)</a>` : ''}`)).join('');
    const groups = {};
    o.relations.forEach((r) => { (groups[r.type] = groups[r.type] || []).push(r.to); });
    const rels = Object.entries(groups).map(([type, ids]) => `
      <h4 class="mt-3 mb-1 text-sm font-semibold text-gray-500 uppercase dark:text-gray-400">${esc(REL[type] || type)}</h4>
      <div class="flex flex-wrap gap-2">${ids.map((t) => `<a href="#/o/${t}" class="px-3 py-1 text-sm rounded-lg bg-gray-100 text-gray-800 hover:bg-primary-100 dark:bg-gray-700 dark:text-gray-200 dark:hover:bg-gray-600">${esc(DB.objects[t].t)}</a>`).join('')}</div>`).join('');
    const chapters = o.chapters.map((c) => `
      <details class="group border-b border-gray-200 dark:border-gray-700 py-3">
        <summary class="cursor-pointer font-medium text-gray-900 dark:text-white">${esc(c.heading)}</summary>
        <div class="mt-2 text-gray-700 dark:text-gray-300 prose-pocket"><p>${esc(c.text)}</p></div></details>`).join('');
    const examples = (o.examples || []).map((ex) => `
      <div class="p-3 mt-2 rounded-lg border ${ex.benign ? 'border-green-300 dark:border-green-700' : 'border-orange-300 dark:border-orange-700'}">
        <p class="text-sm text-gray-500 dark:text-gray-400">${esc(ex.context)}</p>
        <p class="mt-1 italic text-gray-900 dark:text-white">“${esc(ex.utterance)}”</p>
        <p class="mt-2 text-sm">${badge(ex.benign ? 'benign' : 'manipulative', ex.benign ? 'green' : 'orange')} ${esc(ex.marker)}</p>
        ${ex.response ? `<p class="mt-1 text-sm text-gray-700 dark:text-gray-300"><b>Response:</b> ${esc(ex.response)}</p>` : ''}</div>`).join('');
    const src = `${o.provenance.repository}/blob/${o.provenance.commit}/${o.provenance.path}`;
    view.innerHTML = `
      ${crumbs([{ label: 'Explore', href: '#/explore' }, { label: cat.name, href: '#/explore?cat=' + catIdx }, { label: o.title }])}
      <div class="grid gap-4 xl:grid-cols-3">
        <div class="grid gap-4 xl:col-span-2 content-start">
          ${card(`
            <div class="flex flex-wrap gap-2 mb-2">${badge(o.kind, 'blue')}${o.code ? badge(o.code, 'gray') : ''}${capBadges(o.caps)}</div>
            <h1 class="text-2xl font-bold text-gray-900 sm:text-3xl dark:text-white" data-title>${esc(o.title)}</h1>
            ${o.aliases.length ? `<p class="mt-1 text-sm text-gray-500 dark:text-gray-400">Also: ${esc(o.aliases.join(' · '))}</p>` : ''}
            <p class="mt-3 text-lg text-gray-700 dark:text-gray-300">${esc(o.definition)}</p>
            ${o.caps.includes('quiz') ? `<a href="#/quiz?obj=${i}" class="inline-block mt-4 text-white bg-primary-700 hover:bg-primary-800 focus:ring-4 focus:ring-primary-300 font-medium rounded-lg text-sm px-4 py-2.5">Take the quiz</a>` : ''}`)}
          ${toolSections(o, route().params)}
          ${roles}
          ${examples ? card(`<h3 class="text-xl font-bold text-gray-900 dark:text-white">Examples from the catalogue</h3>${examples}`) : ''}
          ${chapters ? card(`<h3 class="text-xl font-bold text-gray-900 dark:text-white mb-1">Chapters</h3>${chapters}`) : ''}
        </div>
        <div class="grid gap-4 content-start">
          ${o.relations.length ? Viz.frame('neighbours', 'Neighbourhood', 'Grouped by relation; click an object to open it') : ''}
          ${rels ? card(`<h3 class="text-xl font-bold text-gray-900 dark:text-white">Links</h3>${rels}`) : ''}
          ${card(`<h3 class="text-xl font-bold text-gray-900 dark:text-white mb-2">Source</h3>
            <p class="text-sm text-gray-600 dark:text-gray-300 break-all">${esc(o.provenance.path)}</p>
            <a class="inline-block mt-2 text-sm text-primary-600 dark:text-primary-400 hover:underline" href="${esc(src)}">Read the page at the pinned commit →</a>
            <p class="mt-3 text-xs text-gray-500 dark:text-gray-400">Object ${i} in the pack, the same index as on the device.</p>`)}
        </div>
      </div>`;
    if (o.relations.length) Viz.afterPaint(() => renderNeighbours(i, o, groups));
    bindTools(i, o);
    window.scrollTo(0, 0);
  }

  function renderNeighbours(i, o, groups) {
    const chart = Viz.ecInit('neighbours');
    if (!chart) return;
    const t = Viz.theme();
    const children = Object.entries(groups).map(([type, ids]) => ({
      name: `${REL[type] || type} (${ids.length})`,
      children: ids.slice(0, 25).map((id) => ({ name: DB.objects[id].t, obj: id })),
    }));
    chart.setOption({
      tooltip: { trigger: 'item', triggerOn: 'mousemove' },
      series: [{
        type: 'tree', data: [{ name: o.title, children }], top: '4%', left: '18%', bottom: '4%', right: '30%',
        symbolSize: 8, initialTreeDepth: 2, expandAndCollapse: true, roam: 'move',
        label: { position: 'left', verticalAlign: 'middle', align: 'right', color: t.ink, fontSize: 11, overflow: 'truncate', width: 110 },
        leaves: { label: { position: 'right', align: 'left' } },
        lineStyle: { color: t.grid },
      }],
    });
    chart.on('click', (p) => { if (p.data && p.data.obj !== undefined) go('/o/' + p.data.obj); });
  }

  /* ------------------------------------------------------------------ detector */

  const STATES = [['observed', 'Yes, observed', 'green'], ['not observed', 'No', 'gray'], ['unknown', "Don't know", 'blue'], ['not applicable', 'Not applicable', 'purple']];

  async function detector(i) {
    markActive('detect');
    const o = await object(i);
    const marks = [];
    const step = () => {
      const k = marks.length;
      if (k >= o.indicators.length) return result();
      view.innerHTML = `
        ${crumbs([{ label: 'Explore', href: '#/explore?cap=detect' }, { label: o.title, href: '#/o/' + i }, { label: 'Detector' }])}
        ${card(`
          <p class="text-sm text-gray-500 dark:text-gray-400">Marker ${k + 1} of ${o.indicators.length} · ${esc(o.title)}</p>
          <div class="w-full bg-gray-200 rounded-full h-2 my-3 dark:bg-gray-700"><div class="bg-orange-600 h-2 rounded-full" style="width:${(100 * k / o.indicators.length).toFixed(0)}%"></div></div>
          <h2 class="text-xl font-semibold text-gray-900 dark:text-white" data-marker>${esc(o.indicators[k])}</h2>
          <p class="mt-2 text-gray-500 dark:text-gray-400">Have you observed this?</p>
          <div class="flex flex-wrap gap-2 mt-4">${STATES.map(([s, label]) => `<button type="button" data-mark="${s}" class="px-4 py-2.5 text-sm font-medium rounded-lg border border-gray-300 hover:bg-gray-100 dark:border-gray-600 dark:text-white dark:hover:bg-gray-700">${label}</button>`).join('')}</div>`)}`;
      view.querySelectorAll('[data-mark]').forEach((b) => b.addEventListener('click', () => { marks.push(b.getAttribute('data-mark')); step(); }));
    };
    const result = () => {
      const count = (s) => marks.filter((m) => m === s).length;
      const section = (k, title) => o[k] && o[k].length ? `<h3 class="mt-5 mb-2 text-lg font-bold text-gray-900 dark:text-white">${title}</h3><ul class="space-y-1 text-gray-700 dark:text-gray-300 list-disc list-inside">${o[k].map((x) => `<li>${esc(x)}</li>`).join('')}</ul>` : '';
      view.innerHTML = `
        ${crumbs([{ label: 'Explore', href: '#/explore?cap=detect' }, { label: o.title, href: '#/o/' + i }, { label: 'Your observations' }])}
        ${card(`
          <h1 class="text-2xl font-bold text-gray-900 dark:text-white">Your observations</h1>
          <p class="mt-1 text-gray-500 dark:text-gray-400">${esc(o.title)}</p>
          <div class="grid grid-cols-2 gap-3 mt-4 sm:grid-cols-4" data-result-counts>
            ${STATES.map(([s, label]) => `<div class="p-3 rounded-lg bg-gray-50 dark:bg-gray-700"><div class="text-2xl font-bold text-gray-900 dark:text-white">${count(s)}</div><div class="text-sm text-gray-500 dark:text-gray-400">${label}</div></div>`).join('')}
          </div>
          <div class="p-4 mt-4 text-sm text-blue-800 rounded-lg bg-blue-50 dark:bg-gray-700 dark:text-blue-300" role="note">
            These are your marks only: not a score, not a probability, not a diagnosis of anyone. Absent markers are not evidence against the pattern, and unknowns stay unknown.</div>
          ${section('alternatives', 'Rule out first')}${section('validation', 'Validate')}${section('actions', 'Next steps')}${section('escalation', 'When to escalate')}
          <div class="flex gap-2 mt-6">
            <a href="#/o/${i}" class="text-white bg-primary-700 hover:bg-primary-800 font-medium rounded-lg text-sm px-4 py-2.5 dark:bg-primary-600">Back to the article</a>
            <a href="#/detect/${i}?again=${Date.now()}" class="px-4 py-2.5 text-sm font-medium text-gray-900 bg-white border border-gray-200 rounded-lg hover:bg-gray-100 dark:bg-gray-800 dark:text-gray-400 dark:border-gray-700">Start again</a>
          </div>`)}`;
    };
    step();
  }

  /* ------------------------------------------------------------------ quiz */

  function quiz(params) {
    let pool = QUIZ.map((q, n) => n);
    const obj = params.get('obj');
    if (obj !== null) pool = pool.filter((n) => { const q = QUIZ[n]; return (q.kind === 1 ? q.options[q.answer] : q.options[0]) === +obj; });
    else pool = pool.filter((n) => QUIZ[n].kind === +(params.get('kind') || 1));
    markActive(obj !== null ? 'explore' : params.get('kind') === '2' ? 'quiz2' : 'quiz1');
    if (!pool.length) { crumbs([{ label: 'Overview', href: '#/' }, { label: 'Quiz' }]); view.innerHTML = card('<p class="text-gray-500 dark:text-gray-400">No questions here.</p>'); return; }
    const n = params.get('q') !== null ? +params.get('q') : pool[Math.floor(Math.random() * pool.length)];
    const q = QUIZ[n];
    const subject = q.kind === 1 ? q.options[q.answer] : q.options[0];
    const answer = (pick) => {
      const ok = pick === q.answer;
      const s = DB.objects[subject];
      const verdict = q.kind === 2 ? (q.answer ? 'This example is benign.' : 'This example is manipulative.') : `The term was: ${s.t}`;
      view.querySelector('[data-quiz-answer]').innerHTML = `
        <div class="p-4 mt-4 rounded-lg ${ok ? 'bg-green-50 text-green-800 dark:bg-gray-700 dark:text-green-400' : 'bg-orange-50 text-orange-800 dark:bg-gray-700 dark:text-orange-400'}" data-verdict="${ok ? 'correct' : 'wrong'}">
          <p class="text-lg font-semibold">${ok ? 'Correct' : 'Not quite'}</p><p>${esc(verdict)}</p>
          ${q.explanation ? `<p class="mt-2"><b>Why:</b> ${esc(q.explanation)}</p>` : ''}
          ${q.response ? `<p class="mt-1"><b>A good response:</b> ${esc(q.response)}</p>` : ''}
        </div>
        <div class="flex gap-2 mt-4">
          <a href="#/o/${subject}" class="px-4 py-2.5 text-sm font-medium text-gray-900 bg-white border border-gray-200 rounded-lg hover:bg-gray-100 dark:bg-gray-800 dark:text-gray-400 dark:border-gray-700">Open “${esc(s.t)}”</a>
          <a href="#/quiz?${obj !== null ? 'obj=' + obj : 'kind=' + q.kind}&r=${Date.now()}" class="text-white bg-primary-700 hover:bg-primary-800 font-medium rounded-lg text-sm px-4 py-2.5 dark:bg-primary-600" data-next>Next question</a>
        </div>`;
      view.querySelectorAll('[data-pick]').forEach((b) => { b.disabled = true; });
    };
    const options = q.kind === 2
      ? [[0, 'Manipulative'], [1, 'Benign']]
      : q.options.map((id, k) => [k, DB.objects[id].t]);
    view.innerHTML = `
      ${crumbs([{ label: 'Overview', href: '#/' }, { label: q.kind === 2 ? 'Quiz: manipulative or benign?' : 'Quiz: which term?' }])}
      ${card(`
        <p class="text-sm text-gray-500 dark:text-gray-400">${q.kind === 2 ? 'Manipulative or benign?' : 'Which term is this? The name is masked.'}</p>
        ${q.context ? `<p class="mt-2 text-sm text-gray-500 dark:text-gray-400">${esc(q.context)}</p>` : ''}
        <blockquote class="p-4 my-3 border-s-4 border-gray-300 bg-gray-50 dark:border-gray-500 dark:bg-gray-700">
          <p class="text-lg ${q.kind === 2 ? 'italic' : ''} text-gray-900 dark:text-white" data-prompt>${esc(q.prompt)}</p></blockquote>
        <div class="grid gap-2 sm:grid-cols-2">${options.map(([k, label]) => `<button type="button" data-pick="${k}" class="p-3 text-left text-sm font-medium rounded-lg border border-gray-300 hover:bg-gray-100 dark:border-gray-600 dark:text-white dark:hover:bg-gray-700">${esc(label)}</button>`).join('')}</div>
        <div data-quiz-answer></div>`)}`;
    view.querySelectorAll('[data-pick]').forEach((b) => b.addEventListener('click', () => answer(+b.getAttribute('data-pick'))));
  }

  /* ------------------------------------------------------------------ router */

  async function render() {
    const { parts, params } = route();
    try {
      if (parts[0] === 'explore') explore(params);
      else if (parts[0] === 'o') await article(+parts[1]);
      else if (parts[0] === 'detect') await detector(+parts[1]);
      else if (parts[0] === 'quiz') quiz(params);
      else if (parts[0] === 'device') await device(params);
      else overview();
    } catch (err) {
      crumbs([{ label: 'Overview', href: '#/' }, { label: 'Error' }]);
      view.innerHTML = card(`<p class="text-orange-700 dark:text-orange-400">Could not load this view: ${esc(err.message)}</p>`);
    }
  }

  document.getElementById('search-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const q = document.getElementById('topbar-search').value.trim();
    go('/explore', new URLSearchParams(q ? { q } : {}));
  });
  document.getElementById('theme-toggle').addEventListener('click', () => {
    const dark = document.documentElement.classList.toggle('dark');
    localStorage.setItem('color-theme', dark ? 'dark' : 'light');
    render();
  });
  window.addEventListener('hashchange', render);

  Promise.all([json('data/index.json'), json('data/quiz.json')]).then(([db, qz]) => {
    DB = db;
    QUIZ = qz;
    document.getElementById('footer-meta').textContent =
      `Pack ${db.pack.id} v${db.pack.version} · Kompendium @ ${String(db.pack.source_commit).slice(0, 10)}`;
    document.body.setAttribute('data-ready', '1');
    render();
  });
})();
