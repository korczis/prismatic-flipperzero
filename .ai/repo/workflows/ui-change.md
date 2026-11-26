# A change to a user interface

Applies to the Pages explorer (`site/`) and to any other web page this repository ships. The
Flipper's own screens are drawn on a 128x64 display and follow `project.ui-atlas` instead.

1. Follow the skill `majordomus://skill/fleet-flowbite-pro`: build on the owner's Flowbite Pro
   components and Tailwind, never ad-hoc styles or another kit.
2. Change `site/src/`, then `just site`: the data from the pack, the build, the Playwright
   end-to-end tests in Chromium and the JS line-coverage gate (`site/coverage.mjs`).
3. A new or changed interaction gets a real-browser test in `site/tests/` in the same change
   (`project.tests-cover-new-code`).
4. Docs and README that describe the page move in the same commit (`project.docs-derived`).
