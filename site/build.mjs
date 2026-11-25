// Builds dist/: Tailwind CSS from src, the app, vendor JS from npm, explorer data from
// tools/explorer.py (build/site/data), device frames and their manifest from docs/evidence.
import { cpSync, mkdirSync, rmSync, existsSync, readFileSync, writeFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const dist = join(here, 'dist');
const data = process.env.EXPLORER_DATA || join(root, 'build/site/data');
if (!existsSync(join(data, 'index.json'))) {
  console.error(`no explorer data in ${data}: run python3 tools/explorer.py first`);
  process.exit(1);
}
rmSync(dist, { recursive: true, force: true });
mkdirSync(join(dist, 'vendor'), { recursive: true });
for (const f of ['index.html', 'app.js', 'viz.js']) cpSync(join(here, 'src', f), join(dist, f));
execFileSync(join(here, 'node_modules/.bin/tailwindcss'),
  ['-c', join(here, 'tailwind.config.js'), '-i', join(here, 'src/input.css'),
    '-o', join(dist, 'app.css'), '--minify'], { cwd: here, stdio: 'inherit' });
cpSync(join(here, 'node_modules/flowbite/dist/flowbite.min.js'), join(dist, 'vendor/flowbite.min.js'));
cpSync(join(here, 'node_modules/echarts/dist/echarts.min.js'), join(dist, 'vendor/echarts.min.js'));
cpSync(data, join(dist, 'data'), { recursive: true });
const shots = join(root, 'docs/evidence/pocket');
cpSync(shots, join(dist, 'screens'), { recursive: true, filter: (p) => !p.endsWith('.json') });
writeFileSync(join(dist, 'data/gallery.json'), readFileSync(join(shots, 'manifest.json')));
mkdirSync(join(dist, 'assets'), { recursive: true });
cpSync(join(root, 'docs/assets/prism.svg'), join(dist, 'assets/prism.svg'));
writeFileSync(join(dist, '.nojekyll'), '');
console.log(`built ${dist}`);
