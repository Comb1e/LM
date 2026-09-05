import { test, expect } from "@playwright/test";
import { mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const artifactDirectory = resolve(dirname(fileURLToPath(import.meta.url)), "../../../build/snake");

async function openGame(page) {
  const response = page.waitForResponse((response) => response.url().endsWith("/api/games") && response.request().method() === "POST");
  await page.goto("/");
  const game = await (await response).json();
  await expect(page.getByRole("button", { name: "Play", exact: true })).toBeEnabled();
  return game.id;
}

async function snapshot(page, id) {
  return (await page.request.get(`/api/games/${id}`)).json();
}

async function setSpeed(page, value) {
  await page.locator("#speed").evaluate((input, value) => {
    input.value = String(value);
    input.dispatchEvent(new Event("input", { bubbles: true }));
    input.dispatchEvent(new Event("change", { bubbles: true }));
  }, value);
  await expect(page.locator("#speed-value")).toHaveText(String(value));
  await expect(page.locator("#restart")).toBeEnabled();
}

test("board renders real assets, moves, and fits the viewport", async ({ page }, info) => {
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("console", (message) => { if (message.type() === "error") errors.push(message.text()); });
  const id = await openGame(page);
  await expect(page.locator(".brand img")).toBeVisible();
  const metrics = await page.locator("#board").evaluate((canvas) => {
    const pixels = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height).data;
    let green = 0, coral = 0;
    for (let index = 0; index < pixels.length; index += 4) {
      const [r, g, b] = pixels.slice(index, index + 3);
      if (r > 65 && g > 125 && b < 150 && g > r * 1.12) green++;
      if (r > 190 && g < 170 && b < 160) coral++;
    }
    return { green, coral, width: canvas.width, image: canvas.toDataURL() };
  });
  expect(metrics.green).toBeGreaterThan(50);
  expect(metrics.coral).toBeGreaterThan(10);
  const layout = await page.evaluate(() => ({
    overflow: document.documentElement.scrollWidth > innerWidth,
    iconCount: document.querySelectorAll("button svg").length,
    imageWidth: document.querySelector(".brand img").naturalWidth,
    overlapping: [...document.querySelectorAll("button")].filter((button) => {
      const bounds = button.getBoundingClientRect();
      if (!bounds.width) return false;
      const walker = document.createTreeWalker(button, NodeFilter.SHOW_TEXT);
      const content = [...button.querySelectorAll("svg")].map((svg) => svg.getBoundingClientRect());
      while (walker.nextNode()) {
        if (!walker.currentNode.textContent.trim()) continue;
        const range = document.createRange();
        range.selectNodeContents(walker.currentNode);
        content.push(...range.getClientRects());
      }
      return content.some((rect) => rect.left < bounds.left - 1 || rect.right > bounds.right + 1 ||
        rect.top < bounds.top - 1 || rect.bottom > bounds.bottom + 1);
    }).map((button) => button.id || button.getAttribute("aria-label")),
  }));
  expect(layout.overflow).toBe(false);
  expect(layout.iconCount).toBeGreaterThan(10);
  expect(layout.imageWidth).toBeGreaterThan(0);
  expect(layout.overlapping).toEqual([]);
  await mkdir(artifactDirectory, { recursive: true });
  await page.screenshot({ path: resolve(artifactDirectory, `${info.project.name}-ready.png`), fullPage: true });
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await expect.poll(async () => (await snapshot(page, id)).ticks).toBeGreaterThan(1);
  const image = await page.locator("#board").evaluate((canvas) => canvas.toDataURL());
  expect(image).not.toEqual(metrics.image);
  await page.screenshot({ path: resolve(artifactDirectory, `${info.project.name}-playing.png`), fullPage: true });
  await page.keyboard.press("p");
  await expect(page.locator("#status")).toHaveText("Paused");
  expect(errors).toEqual([]);
});

test("keyboard reversal, pause, resume, and confirmed restart", async ({ page }) => {
  const id = await openGame(page);
  await setSpeed(page, 4);
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await page.keyboard.press("ArrowUp");
  await expect.poll(async () => (await snapshot(page, id)).direction).toBe(0);
  const before = await snapshot(page, id);
  await page.keyboard.press("ArrowDown");
  await expect.poll(async () => (await snapshot(page, id)).ticks).toBeGreaterThan(before.ticks);
  expect((await snapshot(page, id)).direction).toBe(0);
  await page.keyboard.press("ArrowLeft");
  await page.keyboard.press("ArrowDown");
  await expect.poll(async () => (await snapshot(page, id)).direction).toBe(2);
  await page.keyboard.press("p");
  await expect(page.locator("#status")).toHaveText("Paused");
  const paused = await snapshot(page, id);
  await page.waitForTimeout(400);
  expect(await snapshot(page, id)).toEqual(paused);
  await page.getByRole("button", { name: "Resume", exact: true }).click();
  await expect(page.locator("#status")).toHaveText("In play");
  await page.keyboard.press("r");
  await expect(page.getByRole("dialog")).toBeVisible();
  await expect.poll(async () => (await snapshot(page, id)).status).toBe("paused");
  await page.getByRole("button", { name: "Keep playing" }).click();
  await expect(page.locator("#status")).toHaveText("In play");
  await expect(page.locator("#restart")).toBeEnabled();
  await page.keyboard.press("r");
  await page.locator("#dialog-confirm").click();
  await expect(page.locator("#status")).toHaveText("Ready to play");
  expect((await snapshot(page, id)).ticks).toBe(0);
});

test("settings, wrapping, run history, and connection recovery", async ({ page }) => {
  const id = await openGame(page);
  await page.getByRole("button", { name: "Wrap", exact: true }).click();
  await expect(page.getByRole("button", { name: "Wrap", exact: true })).toHaveAttribute("aria-pressed", "true");
  await setSpeed(page, 16);
  await page.getByRole("button", { name: "Ice", exact: true }).click();
  await expect(page.locator("#color-name")).toHaveText("Ice");
  await page.getByRole("button", { name: "Enable sound" }).click();
  await expect(page.getByRole("button", { name: "Mute sound" })).toHaveAttribute("aria-pressed", "true");
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await expect.poll(async () => (await snapshot(page, id)).ticks).toBeGreaterThan(13);
  expect((await snapshot(page, id)).status).toBe("running");
  await page.keyboard.press("p");
  await expect(page.locator("#status")).toHaveText("Paused");
  await expect(page.getByRole("button", { name: "Classic", exact: true })).toBeEnabled();
  await page.getByRole("button", { name: "Classic", exact: true }).click();
  await page.locator("#dialog-confirm").click();
  await expect(page.locator("#status")).toHaveText("Ready to play");
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await expect(page.locator("#status")).toHaveText("Run ended");
  await expect(page.locator("#rounds")).toHaveText("01");
  await expect(page.locator("#history .run-mode")).toHaveText("Classic");
  await page.getByRole("button", { name: "Play again", exact: true }).click();
  await expect(page.locator("#status")).toHaveText("In play");
  await page.route("**/api/games/**", (route) => route.abort());
  await expect(page.locator("#status")).toHaveText("Disconnected");
  await page.unroute("**/api/games/**");
  await page.getByRole("button", { name: "Reconnect", exact: true }).click();
  await expect(page.locator("#status")).toHaveText("Paused");
  expect((await snapshot(page, id)).status).toBe("paused");
  await page.reload();
  await expect(page.locator("#color-name")).toHaveText("Ice");
  await expect(page.locator("#rounds")).toHaveText("01");
});

test("touch buttons and swipes steer the native game", async ({ page }, info) => {
  test.skip(!["mobile", "narrow"].includes(info.project.name), "Touch controls are shown on phones");
  const id = await openGame(page);
  await setSpeed(page, 4);
  await page.getByRole("button", { name: "Move up", exact: true }).tap();
  await expect.poll(async () => (await snapshot(page, id)).direction).toBe(0);
  const box = await page.locator("#board").boundingBox();
  await page.mouse.move(box.x + box.width * .6, box.y + box.height * .6);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * .3, box.y + box.height * .6);
  await page.mouse.up();
  await expect.poll(async () => (await snapshot(page, id)).direction).toBe(3);
  await page.keyboard.press("p");
  await expect(page.locator("#status")).toHaveText("Paused");
});
