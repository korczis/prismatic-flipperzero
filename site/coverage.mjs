// Line coverage of src/app.js and src/viz.js from the V8 coverage Playwright collected.
// A line counts as run when some covered byte range contains it and no uncovered range of
// count 0 contains all of it. Fails listing each line no test ran.
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const dir = 'test-results/v8';
const files = { 'app.js': 'src/app.js', 'viz.js': 'src/viz.js' };
let bad = 0;
for (const [name, path] of Object.entries(files)) {
  const src = readFileSync(path, 'utf8');
  const starts = [0];
  for (let i = 0; i < src.length; i++) if (src[i] === '\n') starts.push(i + 1);
  const lineOf = (off) => { let lo = 0, hi = starts.length - 1; while (lo < hi) { const m = (lo + hi + 1) >> 1; if (starts[m] <= off) lo = m; else hi = m - 1; } return lo; };
  const hit = new Uint8Array(starts.length);
  const entries = readdirSync(dir).flatMap((f) => JSON.parse(readFileSync(join(dir, f), 'utf8')))
    .filter((e) => e.url.endsWith('/' + name));
  if (!entries.length) { console.error(`${name}: no coverage collected`); bad++; continue; }
  for (const e of entries) {
    // Per run: a byte's count is that of the innermost (last listed, smallest) range holding it.
    const count = new Int32Array(src.length).fill(-1);
    for (const fn of e.functions) for (const r of fn.ranges) count.fill(r.count, r.startOffset, r.endOffset);
    for (let i = 0; i < src.length; i++) if (count[i] > 0 && !/\s/.test(src[i])) hit[lineOf(i)] = 1;
  }
  const missed = [];
  starts.forEach((s, ln) => {
    const text = src.slice(s, starts[ln + 1] ?? src.length).trim();
    const code = text && !/^(\/\/|\/\*|\*|[)}\]];?,?|}\)?\(\);?|'use strict';)/.test(text);
    if (code && !hit[ln]) missed.push(ln + 1);
  });
  console.log(`${path}: ${starts.length - missed.length}/${starts.length} lines`);
  if (missed.length) { console.error(`${path}: lines no test ran: ${missed.join(', ')}`); bad++; }
}
process.exit(bad ? 1 : 0);
