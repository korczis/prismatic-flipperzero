import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: 'tests',
  timeout: 30000,
  reporter: [['list']],
  use: { baseURL: 'http://127.0.0.1:4791/', browserName: 'chromium' },
  webServer: {
    command: 'python3 -m http.server 4791 --bind 127.0.0.1 --directory dist',
    url: 'http://127.0.0.1:4791/index.html',
    reuseExistingServer: false,
  },
});
