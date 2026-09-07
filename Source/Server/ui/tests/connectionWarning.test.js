import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const appSource = readFileSync(
  new URL("../src/App.svelte", import.meta.url),
  "utf8",
);

test("an additional plug-in connection produces a dismissible alert", () => {
  assert.match(appSource, /onFromCpp\("setConnectionWarning"/);
  assert.match(appSource, /role="alert"/);
  assert.match(appSource, /Additional Fiddle plug-in rejected/);
  assert.match(appSource, /aria-label="Dismiss connection warning"/);
  assert.match(appSource, /dispatchCpp\("dismissConnectionWarning"\)/);
});
