import test from 'node:test';
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import { once } from 'node:events';
import {
  toRgb565,
  rgb565ToHex,
  getLuminance,
  getContrastFontColor,
  DeviceSkin,
  DeviceSkinManager,
  BUILTIN_MK20_SKINS,
  Mk20SkinUxController,
  Mk20LabTransport,
  encodeLegacyPreview,
  Mk20Fault
} from '../src/index.mjs';

test('color conversion: toRgb565 and rgb565ToHex handle valid hex, objects and boundary values', () => {
  assert.equal(toRgb565('#000000'), 0x0000);
  assert.equal(toRgb565('#ffffff'), 0xffff);
  assert.equal(toRgb565('#FFF'), 0xffff);
  assert.equal(toRgb565('#f80000'), 0xf800); // 5-bit red
  assert.equal(toRgb565({ r: 255, g: 255, b: 255 }), 0xffff);
  assert.equal(toRgb565({ r: 0, g: 0, b: 0 }), 0x0000);
  assert.equal(toRgb565(0x1234), 0x1234);

  assert.equal(rgb565ToHex(0x0000), '#000000');
  assert.equal(rgb565ToHex(0xffff), '#ffffff');

  // Roundtrip preservation
  const hex = '#10b981';
  const rgb565 = toRgb565(hex);
  assert.ok(rgb565 > 0 && rgb565 < 0xffff);
  const backHex = rgb565ToHex(rgb565);
  assert.equal(typeof backHex, 'string');
  assert.match(backHex, /^#[0-9a-f]{6}$/);

  // Errors on invalid input
  assert.throws(() => toRgb565('invalid'), /invalid_color/);
  assert.throws(() => toRgb565('#12'), /invalid_color/);
  assert.throws(() => toRgb565({ r: 300, g: 0, b: 0 }), /invalid_color/);
  assert.throws(() => rgb565ToHex(-1), /invalid_rgb565/);
  assert.throws(() => rgb565ToHex(70000), /invalid_rgb565/);
});

test('built-in skins: MK20 provides 5 distinct built-in skins with complete theme properties', () => {
  assert.equal(BUILTIN_MK20_SKINS.length, 5);
  const ids = BUILTIN_MK20_SKINS.map(s => s.id);
  assert.deepEqual(ids, ['slate-dark', 'matrix-emerald', 'cyberpunk-neon', 'amber-crt', 'high-contrast']);

  for (const skin of BUILTIN_MK20_SKINS) {
    assert.ok(skin.isBuiltin);
    assert.ok(skin.name.length > 0);
    assert.ok(skin.description.length > 0);
    assert.equal(skin.version, '1.0.0');

    // Top theme tokens
    assert.match(skin.theme.top.background, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.top.card, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.top.border, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.top.text, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.top.textDim, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.top.accent, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.top.status.active, /^#[0-9a-f]{6}$/);

    // Keys theme tokens
    assert.match(skin.theme.keys.defaultBg, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.keys.defaultBorder, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.keys.filledBg, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.keys.filledText, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.keys.focusedBorder, /^#[0-9a-f]{6}$/);
    assert.match(skin.theme.keys.editingBorder, /^#[0-9a-f]{6}$/);

    // Palette (6 color slots)
    assert.equal(skin.theme.palette.length, 6);
    skin.theme.palette.forEach(c => assert.match(c, /^#[0-9a-f]{6}$/));

    // RGB565 lookups
    assert.ok(skin.getRgb565('top.background') >= 0);
    assert.ok(skin.getRgb565('keys.filledBg') >= 0);
    assert.ok(skin.getRgb565('palette.0') >= 0);

    // Wire tokens
    const tokens = skin.toWireTokens();
    assert.equal(tokens.skinId, skin.id);
    assert.equal(tokens.palette.length, 6);
  }
});

test('DeviceSkinManager: manages multiple skins per device with registration, switching and cycling', () => {
  const manager = new DeviceSkinManager();
  assert.equal(manager.activeSkinId, 'slate-dark');
  assert.equal(manager.getActiveSkin().name, 'Slate Dark');

  // List skins
  const list = manager.listSkins();
  assert.equal(list.length, 5);
  assert.equal(list[0].id, 'slate-dark');
  assert.equal(list[0].active, true);
  assert.equal(list[1].active, false);

  // Switch skin
  const switchRes = manager.setActiveSkin('matrix-emerald');
  assert.equal(switchRes.previousSkinId, 'slate-dark');
  assert.equal(switchRes.activeSkin.id, 'matrix-emerald');
  assert.equal(manager.activeSkinId, 'matrix-emerald');

  // Cycle skin forward
  const cycle1 = manager.cycleSkin(+1);
  assert.equal(cycle1.activeSkin.id, 'cyberpunk-neon');

  // Cycle skin backward
  const cycle2 = manager.cycleSkin(-1);
  assert.equal(cycle2.activeSkin.id, 'matrix-emerald');

  // Custom skin registration
  const custom = manager.registerSkin({
    id: 'nordic-frost',
    name: 'Nordic Frost',
    description: 'Cool arctic blue palette',
    theme: {
      top: { background: '#2e3440', card: '#3b4252', border: '#4c566a', text: '#eceff4', accent: '#88c0d0' },
      keys: { defaultBg: '#3b4252', defaultBorder: '#4c566a', defaultText: '#eceff4', filledBg: '#88c0d0', filledText: '#2e3440' },
      palette: ['#eceff4', '#88c0d0', '#a3be8c', '#ebcb8b', '#bf616a', '#4c566a']
    }
  });
  assert.equal(custom.id, 'nordic-frost');
  assert.equal(manager.listSkins().length, 6);

  manager.setActiveSkin('nordic-frost');
  assert.equal(manager.activeSkinId, 'nordic-frost');

  // Cannot delete active skin
  assert.throws(() => manager.unregisterSkin('nordic-frost'), /active_skin_in_use/);

  manager.setActiveSkin('slate-dark');
  assert.equal(manager.unregisterSkin('nordic-frost'), true);
  assert.equal(manager.hasSkin('nordic-frost'), false);

  // Cannot delete built-in skin
  assert.throws(() => manager.unregisterSkin('slate-dark'), /builtin_skin_protected/);

  // Reject invalid skin ID
  assert.throws(() => manager.setActiveSkin('non-existent'), /skin_not_found/);
});

test('Mk20LabTransport skin integration: emits lab.skin events and decorates preview datagrams', async t => {
  const peer = dgram.createSocket('udp4');
  peer.bind(0, '127.0.0.1');
  await once(peer, 'listening');
  t.after(() => peer.close());

  const transport = new Mk20LabTransport({
    labEnabled: true,
    localAddress: '127.0.0.1',
    targetAddress: '127.0.0.1',
    targetPort: peer.address().port
  });
  t.after(() => transport.close());

  await transport.start();

  // Multi-skin access on transport
  assert.equal(transport.listSkins().length, 5);
  assert.equal(transport.getActiveSkin().id, 'slate-dark');

  // Event emission on skin change
  let eventCaptured = null;
  transport.on('lab.skin', evt => { eventCaptured = evt; });

  transport.setSkin('amber-crt');
  assert.ok(eventCaptured);
  assert.equal(eventCaptured.previousSkinId, 'slate-dark');
  assert.equal(eventCaptured.activeSkin.id, 'amber-crt');
  assert.equal(transport.getActiveSkin().id, 'amber-crt');

  // Cycle skin
  transport.cycleSkin(+1);
  assert.equal(transport.getActiveSkin().id, 'high-contrast');

  // Preview automatic skin decoration
  const previewPromise = once(peer, 'message');
  await transport.preview({
    title: 'Theme Test',
    lines: ['Testing active theme preview'],
    scroll: 0,
    totalLines: 1,
    volume: 50,
    muted: false,
    keys: [{ id: 1, main: 'K1' }]
  });

  const [msg] = await previewPromise;
  const decoded = JSON.parse(msg.toString('utf8'));
  assert.equal(decoded.type, 'v2_sync');
  assert.equal(decoded.skinId, 'high-contrast');
  assert.equal(decoded.skinName, 'High Contrast');
});

test('Mk20SkinUxController: enables hardware key and rotary knob skin selection in existing UX flow', () => {
  const ux = new Mk20SkinUxController();

  // Initial state in session mode
  assert.equal(ux.viewMode, 'session');
  assert.equal(ux.skinManager.activeSkinId, 'slate-dark');
  let view = ux.renderView();
  assert.equal(view.mode, 'session');
  assert.equal(view.skinId, 'slate-dark');
  const k2 = view.keys.find(k => k.id === 2);
  assert.equal(k2.top, 'SYSTEM');
  assert.equal(k2.main, 'Settings');

  // Press Key 2 -> Transitions to Settings mode
  const openRes = ux.handleInput({ kind: 'button', button: 'key-2', pressed: true });
  assert.equal(openRes.handled, true);
  assert.equal(openRes.action, 'open_settings');
  assert.equal(ux.viewMode, 'settings');

  view = ux.renderView();
  assert.equal(view.mode, 'settings');
  assert.match(view.title, /Settings/);
  assert.match(view.subtitle, /Slate Dark/);

  // Settings view keys:
  // K17 is Close (Return), K18 is Cycle Skin, K14 is Browse Themes, K10 is Reset Default
  const k17 = view.keys.find(k => k.id === 17);
  const k18 = view.keys.find(k => k.id === 18);
  const k14 = view.keys.find(k => k.id === 14);
  const k10 = view.keys.find(k => k.id === 10);
  assert.equal(k17.main, 'Close');
  assert.equal(k18.top, 'SKIN');
  assert.equal(k18.main, 'Cycle');
  assert.equal(k14.top, 'THEME');
  assert.equal(k14.main, 'Browse');
  assert.equal(k10.top, 'RESET');

  // Row 2 choice keys (K19, K15, K11, K7, K3) represent the 5 skins
  const k19 = view.keys.find(k => k.id === 19);
  const k15 = view.keys.find(k => k.id === 15);
  const k11 = view.keys.find(k => k.id === 11);
  const k7 = view.keys.find(k => k.id === 7);
  const k3 = view.keys.find(k => k.id === 3);

  assert.equal(k19.main, 'Slate');
  assert.equal(k15.main, 'Matrix');
  assert.equal(k11.main, 'Cyberpunk');
  assert.equal(k7.main, 'Amber');
  assert.equal(k3.main, 'High');

  // Active skin (Slate) has flag 1 (isFilled)
  assert.ok((k19.flags & 1) !== 0, 'K19 Slate is filled');
  assert.ok((k15.flags & 1) === 0, 'K15 Matrix is not filled');

  // Press K15 (Matrix Emerald) -> Directly applies Matrix Emerald!
  const selectRes = ux.handleInput({ kind: 'button', button: 'key-15', pressed: true });
  assert.equal(selectRes.handled, true);
  assert.equal(selectRes.action, 'select_skin');
  assert.equal(selectRes.skin.id, 'matrix-emerald');
  assert.equal(ux.skinManager.activeSkinId, 'matrix-emerald');

  view = ux.renderView();
  assert.match(view.subtitle, /Matrix Emerald/);
  const updatedK15 = view.keys.find(k => k.id === 15);
  assert.ok((updatedK15.flags & 1) !== 0, 'K15 Matrix is now filled');

  // Test Key 18 (SKIN / Cycle) -> Cycles to Cyberpunk Neon
  const cycleRes = ux.handleInput({ kind: 'button', button: 'key-18', pressed: true });
  assert.equal(cycleRes.handled, true);
  assert.equal(cycleRes.action, 'cycle_skin');
  assert.equal(cycleRes.skin.id, 'cyberpunk-neon');

  // Test Left Knob rotation (scrolls focused skin)
  const knobRes = ux.handleInput({ kind: 'knob-turn', knob: 'left', delta: +1 });
  assert.equal(knobRes.handled, true);
  assert.equal(knobRes.action, 'scroll_skin');

  // Test Left Knob click (applies the focused skin)
  const clickRes = ux.handleInput({ kind: 'knob-click', knob: 'left' });
  assert.equal(clickRes.handled, true);
  assert.equal(clickRes.action, 'apply_skin');

  // Test Key 10 (Reset to Default Slate)
  const resetRes = ux.handleInput({ kind: 'button', button: 'key-10', pressed: true });
  assert.equal(resetRes.handled, true);
  assert.equal(resetRes.skin.id, 'slate-dark');

  // Test Key 17 (Close Settings -> returns to Session mode)
  const closeRes = ux.handleInput({ kind: 'button', button: 'key-17', pressed: true });
  assert.equal(closeRes.handled, true);
  assert.equal(closeRes.action, 'close_settings');
  assert.equal(ux.viewMode, 'session');

  view = ux.renderView();
  assert.equal(view.mode, 'session');
  assert.equal(view.skinId, 'slate-dark');
});

test('luminance and contrast font reversal: dynamically adapts text color for readability', () => {
  // Pure black and pure white luminance
  assert.equal(getLuminance('#000000'), 0);
  assert.equal(getLuminance('#ffffff'), 255);

  // Auto reversal enabled:
  // Dark background with dark text should invert to bright white font
  const onBlackDarkFg = getContrastFontColor('#000000', '#101010', true);
  assert.equal(onBlackDarkFg, '#ffffff');

  // Bright background with light text should invert to dark black font
  const onWhiteLightFg = getContrastFontColor('#ffffff', '#e0e0e0', true);
  assert.equal(onWhiteLightFg, '#000000');

  // When autoReverse is false, original defaultFg is preserved regardless of background
  const preserved = getContrastFontColor('#ffffff', '#ffffff', false);
  assert.equal(preserved, '#ffffff');
});

test('extended design elements: all built-in skins specify fonts, button styles, gradients, and backgrounds', () => {
  for (const skin of BUILTIN_MK20_SKINS) {
    // 1. Fonts (Korean / English)
    assert.equal(skin.fonts.korean, 'D2Coding');
    assert.equal(skin.fonts.english, 'D2Coding');
    assert.ok(skin.fonts.sizeTitle > 0);
    assert.ok(skin.fonts.sizeMain > 0);
    assert.ok(skin.fonts.sizeSub > 0);
    assert.ok(skin.fonts.sizeDebug > 0);

    // 2. Button Style
    assert.equal(typeof skin.buttonStyle.lineVisible, 'boolean');
    assert.match(skin.buttonStyle.lineColor, /^#[0-9a-f]{6}$/);
    assert.equal(typeof skin.buttonStyle.fillVisible, 'boolean');
    assert.match(skin.buttonStyle.fillColor, /^#[0-9a-f]{6}$/);

    // 3. Title Style
    assert.equal(typeof skin.titleStyle.lineVisible, 'boolean');
    assert.match(skin.titleStyle.lineColor, /^#[0-9a-f]{6}$/);
    assert.equal(typeof skin.titleStyle.fillVisible, 'boolean');
    assert.match(skin.titleStyle.fillColor, /^#[0-9a-f]{6}$/);
    assert.match(skin.titleStyle.fontColor, /^#[0-9a-f]{6}$/);

    // 4. Row Background Color Group (4 rows: 0..3)
    assert.equal(skin.rowBgColors.length, 4);
    skin.rowBgColors.forEach(c => assert.match(c, /^#[0-9a-f]{6}$/));

    // 5. Individual Button Background Color
    assert.ok(typeof skin.individualButtonBg === 'object');
    for (const [kid, color] of Object.entries(skin.individualButtonBg)) {
      const id = Number(kid);
      assert.ok(id >= 1 && id <= 20);
      assert.match(color, /^#[0-9a-f]{6}$/);
    }

    // 6. Gradation support for button background color
    assert.ok(['none', 'vertical', 'horizontal', 'LTtoRB', 'RTtoLB'].includes(skin.gradient.type));
    assert.match(skin.gradient.startColor, /^#[0-9a-f]{6}$/);
    assert.match(skin.gradient.endColor, /^#[0-9a-f]{6}$/);

    // 7. Button Bottom Message = Debug, Debug visible (Off for all by default)
    assert.equal(skin.debug.visible, false);

    // 8. Button Font color & Auto Reverse
    assert.match(skin.fontColors.title, /^#[0-9a-f]{6}$/);
    assert.match(skin.fontColors.main, /^#[0-9a-f]{6}$/);
    assert.match(skin.fontColors.sub, /^#[0-9a-f]{6}$/);
    assert.equal(skin.fontColors.autoReverse, true);

    // 9. Wire tokens verification
    const tokens = skin.toWireTokens();
    assert.equal(tokens.fontKorean, 'D2Coding');
    assert.equal(tokens.fontEnglish, 'D2Coding');
    assert.equal(typeof tokens.btnLineVisible, 'boolean');
    assert.ok(tokens.btnLineColor >= 0 && tokens.btnLineColor <= 0xffff);
    assert.equal(typeof tokens.btnFillVisible, 'boolean');
    assert.ok(tokens.btnFillColor >= 0 && tokens.btnFillColor <= 0xffff);
    assert.equal(typeof tokens.titleLineVisible, 'boolean');
    assert.ok(tokens.titleLineColor >= 0 && tokens.titleLineColor <= 0xffff);
    assert.equal(typeof tokens.titleFillVisible, 'boolean');
    assert.ok(tokens.titleFillColor >= 0 && tokens.titleFillColor <= 0xffff);
    assert.ok(tokens.titleFontColor >= 0 && tokens.titleFontColor <= 0xffff);
    assert.ok(tokens.btnMainFontColor >= 0 && tokens.btnMainFontColor <= 0xffff);
    assert.ok(tokens.btnSubFontColor >= 0 && tokens.btnSubFontColor <= 0xffff);
    assert.equal(tokens.autoFontReverse, true);
    assert.equal(tokens.rowBgColors.length, 4);
    assert.ok(tokens.gradientType >= 0 && tokens.gradientType <= 4);
    assert.ok(tokens.gradientStart >= 0 && tokens.gradientStart <= 0xffff);
    assert.ok(tokens.gradientEnd >= 0 && tokens.gradientEnd <= 0xffff);
    assert.equal(tokens.debugVisible, false);
  }
});

test('button background resolution and readable font color lookups', () => {
  const slate = BUILTIN_MK20_SKINS.find(s => s.id === 'slate-dark');

  // Key 4 has individual button background override
  assert.equal(slate.getButtonBackground(4), '#3a1021');
  // Key 16 has individual button background override
  assert.equal(slate.getButtonBackground(16), '#083563');
  // Key 20 has individual button background override
  assert.equal(slate.getButtonBackground(20), '#082d21');

  // Key 17 is Row 0 -> rowBgColors[0]
  assert.equal(slate.getButtonBackground(17), slate.rowBgColors[0]);
  // Key 18 is Row 1 -> rowBgColors[1]
  assert.equal(slate.getButtonBackground(18), slate.rowBgColors[1]);
  // Key 19 is Row 2 -> rowBgColors[2]
  assert.equal(slate.getButtonBackground(19), slate.rowBgColors[2]);
  // Key 1 is Row 0 -> rowBgColors[0]
  assert.equal(slate.getButtonBackground(1), slate.rowBgColors[0]);

  // Readable font color lookup:
  // When defaultFg is dark on dark background, it reverses to bright #ffffff
  const readableDarkBgDarkFg = slate.getReadableFontColor('#080d1a', '#101010');
  assert.equal(readableDarkBgDarkFg, '#ffffff');

  // When defaultFg is already bright on dark background, it preserves the themed color
  const readableDarkBgLightFg = slate.getReadableFontColor('#080d1a', '#e2e8f0');
  assert.equal(readableDarkBgLightFg, slate.fontColors.main);

  // When background is bright (#ffffff), font reverses to black
  const readableLight = slate.getReadableFontColor('#ffffff', '#ffffff');
  assert.equal(readableLight, '#000000');
});

