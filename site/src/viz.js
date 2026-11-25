/* The one chart layer of this site.
 *
 * - ecInit() is the only place that calls echarts.init; it refuses hidden or zero-width boxes,
 *   adds motion (off under prefers-reduced-motion), aria from the box's aria-label and a
 *   ResizeObserver.
 * - Every chart is declared in REGISTRY ({id, page, kind, hierarchy, drill}) and rendered by
 *   the page that owns it. Drill-down is ours (nodeClick false, breadcrumb hidden); a Flowbite
 *   breadcrumb steps back and leaves navigate by hash.
 * - Each chart sits in a frame with a maximize button; Escape restores; the state lives in the
 *   URL (?max=<id>), so a maximized view is a link.
 */
(function () {
  'use strict';

  const REDUCED = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const PALETTE = {
    light: ['#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#e87ba4', '#008300', '#6250d6', '#e34948'],
    dark: ['#3987e5', '#d95926', '#199e70', '#c98500', '#d55181', '#008300', '#9085e9', '#e66767'],
  };
  const REGISTRY = [
    { id: 'kinds', page: 'overview', kind: 'bar', hierarchy: false, drill: 'kind -> explore filtered by kind' },
    { id: 'tree', page: 'overview', kind: 'treemap', hierarchy: true, drill: 'category -> its objects -> article' },
    { id: 'caps', page: 'overview', kind: 'bar', hierarchy: false, drill: 'capability -> explore filtered by capability' },
    { id: 'neighbours', page: 'article', kind: 'tree', hierarchy: true, drill: 'relation group -> object -> article' },
  ];
  const instances = new Map();

  function theme() {
    const dark = document.documentElement.classList.contains('dark');
    return {
      dark,
      palette: dark ? PALETTE.dark : PALETTE.light,
      ink: dark ? '#e5e7eb' : '#111827',
      muted: dark ? '#9ca3af' : '#6b7280',
      grid: dark ? '#374151' : '#e5e7eb',
      surface: dark ? '#1f2937' : '#ffffff',
    };
  }

  function ecInit(id) {
    const el = document.getElementById(id);
    if (!el || el.offsetParent === null || el.clientWidth === 0) return null;
    const old = instances.get(id);
    if (old) old.dispose();
    const chart = echarts.init(el, null, { renderer: 'canvas' });
    const setOption = chart.setOption.bind(chart);
    chart.setOption = (opt, ...rest) => {
      const t = theme();
      const motion = REDUCED
        ? { animation: false }
        : {
            animationDuration: 850, animationEasing: 'cubicOut',
            animationDelay: (i) => Math.min(i * 14, 420), animationDurationUpdate: 380,
          };
      return setOption({
        color: t.palette, textStyle: { color: t.ink, fontFamily: 'Inter, ui-sans-serif, system-ui' },
        aria: { enabled: true, label: { description: el.getAttribute('aria-label') || '' } },
        ...motion, ...opt,
      }, ...rest);
    };
    const ro = new ResizeObserver(() => chart.resize());
    ro.observe(el);
    chart.on('finished', () => el.setAttribute('data-rendered', '1'));
    instances.set(id, chart);
    return chart;
  }

  /* A frame: card, title, maximize. The chart redraws to whatever box it is given. */
  function frame(id, title, subtitle) {
    const entry = REGISTRY.find((r) => r.id === id);
    if (!entry) throw new Error('chart ' + id + ' is not in the registry');
    return `
      <section class="p-4 bg-white rounded-lg shadow sm:p-6 dark:bg-gray-800" data-frame="${id}">
        <div class="flex justify-between items-start mb-3 gap-2">
          <div>
            <h3 class="text-xl font-bold leading-none text-gray-900 dark:text-white">${title}</h3>
            <p class="mt-1 text-sm text-gray-500 dark:text-gray-400">${subtitle}</p>
          </div>
          <button type="button" data-maximize="${id}" class="p-2 text-gray-500 rounded-lg hover:text-gray-900 hover:bg-gray-100 dark:text-gray-400 dark:hover:text-white dark:hover:bg-gray-700" aria-label="Maximize ${title}">
            <svg class="w-5 h-5" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 8V4h4M20 8V4h-4M4 16v4h4M20 16v4h-4"/></svg>
          </button>
        </div>
        <div id="${id}-crumbs"></div>
        <div id="${id}" role="img" aria-label="${title}: ${subtitle}" class="w-full h-80"></div>
      </section>`;
  }

  function maximize(id, on) {
    const box = document.querySelector(`[data-frame="${id}"]`);
    if (!box) return;
    box.classList.toggle('fixed', on);
    box.classList.toggle('inset-2', on);
    box.classList.toggle('z-50', on);
    const chartEl = document.getElementById(id);
    chartEl.classList.toggle('h-80', !on);
    chartEl.style.height = on ? 'calc(100vh - 7rem)' : '';
    document.getElementById('main-content').toggleAttribute('data-maximized', on);
    const chart = instances.get(id);
    if (chart) setTimeout(() => chart.resize(), 30);
    if (on) box.querySelector('[data-maximize]').focus();
  }

  document.addEventListener('click', (e) => {
    const btn = e.target.closest('[data-maximize]');
    if (!btn) return;
    const id = btn.getAttribute('data-maximize');
    const url = new URL(location.href);
    const params = new URLSearchParams(url.hash.split('?')[1] || '');
    if (params.get('max') === id) params.delete('max'); else params.set('max', id);
    const path = url.hash.split('?')[0] || '#/';
    const q = params.toString();
    history.replaceState(null, '', path + (q ? '?' + q : ''));
    maximize(id, params.get('max') === id);
  });
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    const params = new URLSearchParams(location.hash.split('?')[1] || '');
    const id = params.get('max');
    if (!id) return;
    params.delete('max');
    const q = params.toString();
    history.replaceState(null, '', location.hash.split('?')[0] + (q ? '?' + q : ''));
    maximize(id, false);
  });

  /* Charts render after their page is in the DOM: a short timeout, then a settle resize. */
  function afterPaint(fn) {
    setTimeout(() => {
      fn();
      setTimeout(() => instances.forEach((c) => c.resize()), 300);
      const id = new URLSearchParams(location.hash.split('?')[1] || '').get('max');
      if (id) maximize(id, true);
    }, 30);
  }

  window.Viz = { ecInit, frame, afterPaint, theme, REGISTRY, instances, maximize };
})();
