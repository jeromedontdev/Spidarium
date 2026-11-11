import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const landing = await readFile(new URL('../style.css', import.meta.url), 'utf8');
const device = await readFile(new URL('../device.css', import.meta.url), 'utf8');

// Inspect declarations from the real stylesheets, merging repeated exact rules.
function declarations(css, selector) {
  const result = {};
  for (const match of css.replace(/\/\*[\s\S]*?\*\//g, '').matchAll(/([^{}]+)\{([^{}]*)\}/g)) {
    if (!match[1].split(',').some(value => value.trim() === selector)) continue;
    for (const declaration of match[2].split(';')) {
      const colon = declaration.indexOf(':');
      if (colon > 0) result[declaration.slice(0, colon).trim()] = declaration.slice(colon + 1).trim();
    }
  }
  assert.ok(Object.keys(result).length, `Missing style rule: ${selector}`);
  return result;
}

test('flat native panel, row and selection palette has hard two-pixel chrome', () => {
  for (const [selector, background] of [
    ['.d-panel', '#16283c'],
    ['.game-screen .d-row', '#24405a'],
    ['.game-screen .d-row.selected', '#2b6a5f'],
  ]) {
    assert.equal(declarations(device, selector).background, background);
  }
  const panel = declarations(device, '.d-panel');
  assert.equal(panel.border, '2px solid #b99b67');
  assert.equal(panel['border-radius'], '2px');
  assert.equal(panel['box-shadow'], 'inset 2px 2px 0 #395873,inset -2px -2px 0 #0a1220');
  const rule = declarations(device, '.d-panel::before');
  assert.equal(rule.background, '#b99b67');
  assert.ok(parseInt(rule.left) >= 120, 'ornament stays beyond the 96px Back button at x12');
  assert.doesNotMatch(device, /gradient\(|blur\(/i);
  assert.doesNotMatch(device, /(?:background|border):[^;}]*(?:#[\da-f]{8}\b|rgba\()/i);
});

test('pixel inputs, keys, HUD and tiles stay square without shrinking touch geometry', () => {
  for (const selector of ['.d-identity', '.d-stat', '.d-input', '.game-screen .d-key', '.game-screen .d-tile']) {
    assert.equal(declarations(device, selector)['border-radius'], '2px', selector);
  }
  assert.equal(declarations(device, '.game-screen .d-key').height, '42px');
  assert.equal(declarations(device, '.game-screen .d-key').width, '34px');
  assert.equal(declarations(device, '.game-screen .d-row').height, '76px');
  assert.equal(declarations(device, '.game-screen .d-row').width, '376px');
  assert.equal(declarations(device, '.d-row input[type=range].d-slider')['pointer-events'], 'auto');
  assert.equal(declarations(device, '.d-row input[type=range].d-slider').height, '20px');
  assert.equal(declarations(device, '#device-controls')['pointer-events'], 'none');
  assert.equal(declarations(device, '.game-screen .d-button')['pointer-events'], 'auto');
  assert.equal(declarations(device, '.game-screen button:not(:disabled):hover').transform, 'none');
  assert.equal(declarations(device, '.game-viewport')['border-radius'], '44px');
});

test('selection, destructive, disabled and keyboard focus have separate indicators', () => {
  const selected = declarations(device, '.game-screen .d-row.selected');
  const destructive = declarations(device, '.game-screen .d-button.destructive');
  const disabled = declarations(device, '.game-screen .d-button:disabled');
  assert.equal(selected['border-color'], '#76d6b1');
  assert.match(selected['box-shadow'], /inset 4px 0 0 #76d6b1/);
  assert.equal(destructive.background, '#4a2531');
  assert.equal(destructive['border-color'], '#dc8178');
  assert.equal(disabled.background, '#16283c');
  assert.equal(disabled['border-style'], 'dashed');
  assert.equal(disabled.opacity, '1');
  assert.equal(declarations(device, '.game-screen button:focus-visible').outline, '2px solid #e8efda');
  assert.equal(declarations(device, '.game-screen input:focus-visible').outline, '2px solid #e8efda');
  assert.equal(declarations(device, '.game-screen:focus-visible')['outline-offset'], '-4px');
});

test('slider rows reserve separate areas for icon, label, value and track', () => {
  const label = declarations(device, '.d-row.has-slider .row-label');
  const detail = declarations(device, '.d-row.has-slider .row-detail');
  const track = declarations(device, '.d-row.has-slider input[type=range].d-slider');
  const iconTrack = declarations(device, '.d-row.has-slider.has-icon input[type=range].d-slider');
  assert.equal(label.top, '10px');
  assert.equal(label.right, '84px');
  assert.equal(detail.top, '10px');
  assert.equal(detail.left, 'auto');
  assert.equal(detail.width, '60px');
  assert.equal(track.bottom, '12px');
  assert.equal(track.margin, '0');
  assert.equal(iconTrack.left, '72px');
  assert.equal(iconTrack.width, 'calc(100% - 86px)');
});

test('landing chrome shares local Montserrat and palette without filtering artwork', () => {
  const root = declarations(landing, ':root');
  for (const [name, value] of Object.entries({
    '--bg': '#0a1220', '--panel': '#16283c', '--panel2': '#24405a',
    '--ink': '#e8efda', '--muted': '#9aaeb5', '--lime': '#76d6b1', '--line': '#b99b67',
  })) assert.equal(root[name], value, name);
  assert.equal(root['--sans'], 'Montserrat,Arial,sans-serif');
  for (const selector of ['.hero-moss', '.hero-spider']) {
    assert.equal(declarations(landing, selector).filter, 'none');
  }
  for (const selector of ['.button', '.tool-card', 'dialog', '.setting-input', '.game-tile']) {
    assert.equal(declarations(landing, selector)['border-radius'], '2px', selector);
  }
  assert.equal(declarations(landing, 'dialog::backdrop')['backdrop-filter'], 'none');
  assert.equal(declarations(landing, '.hardware-tabs button[aria-selected=true]').background, '#2b6a5f');
  assert.equal(declarations(landing, 'button:disabled')['border-style'], 'dashed');
  assert.equal(declarations(landing, '.danger')['border-color'], '#dc8178');
  assert.equal(declarations(landing, ':focus-visible').outline, '2px solid var(--ink)');
  assert.equal(declarations(landing, 'progress::-webkit-progress-value')['border-radius'], '0');
  assert.equal(declarations(landing, 'progress::-moz-progress-bar')['border-radius'], '0');
});
