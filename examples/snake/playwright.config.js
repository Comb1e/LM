import { defineConfig, devices } from "@playwright/test";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");

export default defineConfig({
  testDir: "./browser-tests",
  timeout: 25000,
  expect: { timeout: 7000 },
  workers: 1,
  reporter: "list",
  use: {
    baseURL: "http://127.0.0.1:4174",
    launchOptions: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE
      ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {},
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
  },
  projects: [
    { name: "desktop", use: { viewport: { width: 1440, height: 900 } } },
    { name: "mobile", use: { ...devices["iPhone 13"], defaultBrowserType: "chromium", viewport: { width: 390, height: 844 } } },
    { name: "narrow", use: { viewport: { width: 320, height: 740 }, isMobile: true, hasTouch: true } },
    { name: "tablet", use: { viewport: { width: 820, height: 1180 } } },
  ],
  webServer: {
    command: "make snake && build/snake/snake --port 4174",
    cwd: root,
    url: "http://127.0.0.1:4174/api/config",
    reuseExistingServer: false,
    timeout: 20000,
    gracefulShutdown: { signal: "SIGINT", timeout: 5000 },
  },
});
