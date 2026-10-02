import { Mk20Fault } from './index.mjs';

/**
 * Validates and converts hex or RGB colors into 16-bit RGB565 integer (0..65535).
 * RGB565: [15:11] Red (5 bits), [10:5] Green (6 bits), [4:0] Blue (5 bits).
 */
export function toRgb565(color) {
  if (typeof color === 'number' && Number.isInteger(color) && color >= 0 && color <= 65535) {
    return color;
  }
  let r = 0, g = 0, b = 0;
  if (typeof color === 'string') {
    const trimmed = color.trim().replace(/^#/, '');
    if (trimmed.length === 3) {
      r = parseInt(trimmed[0] + trimmed[0], 16);
      g = parseInt(trimmed[1] + trimmed[1], 16);
      b = parseInt(trimmed[2] + trimmed[2], 16);
    } else if (trimmed.length === 6) {
      r = parseInt(trimmed.slice(0, 2), 16);
      g = parseInt(trimmed.slice(2, 4), 16);
      b = parseInt(trimmed.slice(4, 6), 16);
    } else {
      throw new Mk20Fault('invalid_color');
    }
    if (Number.isNaN(r) || Number.isNaN(g) || Number.isNaN(b)) {
      throw new Mk20Fault('invalid_color');
    }
  } else if (color && typeof color === 'object') {
    r = color.r; g = color.g; b = color.b;
    if (!Number.isInteger(r) || !Number.isInteger(g) || !Number.isInteger(b) ||
        r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
      throw new Mk20Fault('invalid_color');
    }
  } else {
    throw new Mk20Fault('invalid_color');
  }
  return (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b >> 3) & 0x1F)) >>> 0;
}

/**
 * Converts a 16-bit RGB565 integer back to standard #RRGGBB hex.
 */
export function rgb565ToHex(rgb565) {
  if (!Number.isInteger(rgb565) || rgb565 < 0 || rgb565 > 65535) {
    throw new Mk20Fault('invalid_rgb565');
  }
  const r5 = (rgb565 >> 11) & 0x1F;
  const g6 = (rgb565 >> 5) & 0x3F;
  const b5 = rgb565 & 0x1F;
  const r = Math.round((r5 * 255) / 31);
  const g = Math.round((g6 * 255) / 63);
  const b = Math.round((b5 * 255) / 31);
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
}

/**
 * Calculates perceived luminance (0..255) using ITU-R BT.601 coefficients.
 */
export function getLuminance(color) {
  const rgb565 = toRgb565(color);
  const r5 = (rgb565 >> 11) & 0x1F;
  const g6 = (rgb565 >> 5) & 0x3F;
  const b5 = rgb565 & 0x1F;
  const r = Math.round((r5 * 255) / 31);
  const g = Math.round((g6 * 255) / 63);
  const b = Math.round((b5 * 255) / 31);
  return Math.round((299 * r + 587 * g + 114 * b) / 1000);
}

/**
 * Automatically calculates readable font color (reverses to black on bright, white on dark).
 */
export function getContrastFontColor(bg, defaultFg, autoReverse = true) {
  if (!autoReverse) return normalizeHex(defaultFg);
  const lum = getLuminance(bg);
  if (lum > 140) {
    return '#000000';
  } else {
    if (getLuminance(defaultFg) < 90) {
      return '#ffffff';
    }
    return normalizeHex(defaultFg);
  }
}

/**
 * Normalizes hex string.
 */
function normalizeHex(color) {
  return rgb565ToHex(toRgb565(color));
}

/**
 * Represents a complete UI skin definition for a hardware device.
 */
export class DeviceSkin {
  constructor(definition) {
    if (!definition || typeof definition !== 'object') throw new Mk20Fault('invalid_skin');
    const { id, name, description = '', version = '1.0.0', isBuiltin = false, theme } = definition;
    if (typeof id !== 'string' || !/^[a-z0-9][a-z0-9-_]{1,31}$/.test(id)) {
      throw new Mk20Fault('invalid_skin_id');
    }
    if (typeof name !== 'string' || name.trim().length === 0 || name.length > 40) {
      throw new Mk20Fault('invalid_skin_name');
    }
    if (!theme || typeof theme !== 'object' || !theme.top || !theme.keys) {
      throw new Mk20Fault('invalid_skin_theme');
    }

    this.id = id;
    this.name = name.trim();
    this.description = String(description).slice(0, 128);
    this.version = String(version);
    this.isBuiltin = Boolean(isBuiltin);

    // Validate and freeze top display theme
    const top = theme.top;
    this.theme = {
      top: Object.freeze({
        background: normalizeHex(top.background),
        card: normalizeHex(top.card ?? top.background),
        border: normalizeHex(top.border ?? '#334155'),
        text: normalizeHex(top.text ?? '#FFFFFF'),
        textDim: normalizeHex(top.textDim ?? '#94A3B8'),
        accent: normalizeHex(top.accent ?? '#06B6D4'),
        status: Object.freeze({
          idle: normalizeHex(top.status?.idle ?? '#64748B'),
          active: normalizeHex(top.status?.active ?? '#06B6D4'),
          success: normalizeHex(top.status?.success ?? '#10B981'),
          warning: normalizeHex(top.status?.warning ?? '#F59E0B'),
          error: normalizeHex(top.status?.error ?? '#F43F5E')
        })
      }),
      keys: Object.freeze({
        defaultBg: normalizeHex(theme.keys.defaultBg),
        defaultBorder: normalizeHex(theme.keys.defaultBorder ?? '#334155'),
        defaultText: normalizeHex(theme.keys.defaultText ?? '#FFFFFF'),
        defaultSubText: normalizeHex(theme.keys.defaultSubText ?? '#94A3B8'),
        filledBg: normalizeHex(theme.keys.filledBg ?? '#06B6D4'),
        filledText: normalizeHex(theme.keys.filledText ?? '#FFFFFF'),
        focusedBorder: normalizeHex(theme.keys.focusedBorder ?? '#06B6D4'),
        focusedBg: normalizeHex(theme.keys.focusedBg ?? theme.keys.defaultBg),
        editingBorder: normalizeHex(theme.keys.editingBorder ?? '#F59E0B'),
        editingBg: normalizeHex(theme.keys.editingBg ?? theme.keys.defaultBg),
        disabledBg: normalizeHex(theme.keys.disabledBg ?? '#0F172A'),
        disabledText: normalizeHex(theme.keys.disabledText ?? '#475569')
      }),
      palette: Object.freeze(
        Array.isArray(theme.palette) && theme.palette.length === 6
          ? theme.palette.map(c => normalizeHex(c))
          : ['#FFFFFF', '#06B6D4', '#10B981', '#F59E0B', '#F43F5E', '#94A3B8']
      )
    };

    // Extended Design Elements:
    // 1. Fonts (Korean / English)
    this.fonts = Object.freeze({
      korean: String(definition.fonts?.korean || 'D2Coding'),
      english: String(definition.fonts?.english || 'D2Coding'),
      sizeTitle: Number.isInteger(definition.fonts?.sizeTitle) ? definition.fonts.sizeTitle : 10,
      sizeMain: Number.isInteger(definition.fonts?.sizeMain) ? definition.fonts.sizeMain : 14,
      sizeSub: Number.isInteger(definition.fonts?.sizeSub) ? definition.fonts.sizeSub : 10,
      sizeDebug: Number.isInteger(definition.fonts?.sizeDebug) ? definition.fonts.sizeDebug : 9
    });

    // 2. Button Style: Line visible, Line color, Fill visible, Fill color
    const btnStyle = definition.buttonStyle || {};
    this.buttonStyle = Object.freeze({
      lineVisible: btnStyle.lineVisible !== false,
      lineColor: normalizeHex(btnStyle.lineColor || this.theme.keys.defaultBorder),
      fillVisible: btnStyle.fillVisible !== false,
      fillColor: normalizeHex(btnStyle.fillColor || this.theme.keys.defaultBg)
    });

    // 3. Button Title Style: Line visible, Line color, Fill visible, Fill color, Font color
    const tStyle = definition.titleStyle || {};
    this.titleStyle = Object.freeze({
      lineVisible: tStyle.lineVisible !== false,
      lineColor: normalizeHex(tStyle.lineColor || this.theme.top.border),
      fillVisible: tStyle.fillVisible !== false,
      fillColor: normalizeHex(tStyle.fillColor || this.theme.keys.defaultBg),
      fontColor: normalizeHex(tStyle.fontColor || this.theme.top.accent)
    });

    // 4. Button Row background Color Group (Row 0, 1, 2, 3)
    this.rowBgColors = Object.freeze(
      Array.isArray(definition.rowBgColors) && definition.rowBgColors.length === 4
        ? definition.rowBgColors.map(c => normalizeHex(c))
        : [this.theme.keys.defaultBg, this.theme.keys.defaultBg, this.theme.keys.defaultBg, this.theme.keys.defaultBg]
    );

    // 5. Individual Button background Color
    const indBg = {};
    if (definition.individualButtonBg && typeof definition.individualButtonBg === 'object') {
      for (const [k, v] of Object.entries(definition.individualButtonBg)) {
        const kid = Number(k);
        if (Number.isInteger(kid) && kid >= 1 && kid <= 20) {
          indBg[kid] = normalizeHex(v);
        }
      }
    }
    this.individualButtonBg = Object.freeze(indBg);

    // 6. Gradation support for button background color (Vertical, Horizontal, LTtoRB, RTtoLB)
    const grad = definition.gradient || {};
    const validGrads = ['none', 'vertical', 'horizontal', 'LTtoRB', 'RTtoLB'];
    this.gradient = Object.freeze({
      type: validGrads.includes(grad.type) ? grad.type : 'none',
      startColor: normalizeHex(grad.startColor || this.theme.keys.defaultBg),
      endColor: normalizeHex(grad.endColor || this.theme.keys.defaultBg)
    });

    // 7. Button Bottom Message = Debug, Debug visible (Off for all by default)
    this.debug = Object.freeze({
      visible: Boolean(definition.debug?.visible ?? false)
    });

    // 8. Button Font color & Automatic Font Color reversed by Background
    const fColors = definition.fontColors || {};
    this.fontColors = Object.freeze({
      title: normalizeHex(fColors.title || this.titleStyle.fontColor),
      main: normalizeHex(fColors.main || this.theme.keys.defaultText),
      sub: normalizeHex(fColors.sub || this.theme.keys.defaultSubText),
      autoReverse: fColors.autoReverse !== false
    });

    Object.freeze(this.theme);
    Object.freeze(this);
  }

  getRgb565(path) {
    const parts = path.split('.');
    let val = this;
    for (const p of parts) {
      if (!val || typeof val !== 'object' || !(p in val)) {
        // Fallback to checking inside this.theme
        if (val === this && this.theme && p in this.theme) {
          val = this.theme[p];
          continue;
        }
        throw new Mk20Fault('invalid_token_path');
      }
      val = val[p];
    }
    return toRgb565(val);
  }

  getHex(path) {
    const parts = path.split('.');
    let val = this;
    for (const p of parts) {
      if (!val || typeof val !== 'object' || !(p in val)) {
        if (val === this && this.theme && p in this.theme) {
          val = this.theme[p];
          continue;
        }
        throw new Mk20Fault('invalid_token_path');
      }
      val = val[p];
    }
    if (typeof val !== 'string') throw new Mk20Fault('invalid_token_path');
    return val;
  }

  /**
   * Resolves effective button background color for a specific physical key ID (1..20).
   */
  getButtonBackground(keyId) {
    if (keyId in this.individualButtonBg) {
      return this.individualButtonBg[keyId];
    }
    const row = (keyId - 1) % 4;
    if (this.rowBgColors && this.rowBgColors[row]) {
      return this.rowBgColors[row];
    }
    return this.buttonStyle.fillColor;
  }

  /**
   * Resolves high-contrast readable font color for a given background.
   */
  getReadableFontColor(bg, defaultFg) {
    return getContrastFontColor(bg, defaultFg, this.fontColors.autoReverse);
  }

  toWireTokens() {
    const gradMap = { none: 0, vertical: 1, horizontal: 2, LTtoRB: 3, RTtoLB: 4 };
    return {
      skinId: this.id,
      skinName: this.name,
      fontKorean: this.fonts.korean,
      fontEnglish: this.fonts.english,
      topBg: this.getRgb565('theme.top.background'),
      topCard: this.getRgb565('theme.top.card'),
      topBorder: this.getRgb565('theme.top.border'),
      topText: this.getRgb565('theme.top.text'),
      topDim: this.getRgb565('theme.top.textDim'),
      topAccent: this.getRgb565('theme.top.accent'),
      keyDefaultBg: this.getRgb565('theme.keys.defaultBg'),
      keyDefaultBorder: this.getRgb565('theme.keys.defaultBorder'),
      keyDefaultText: this.getRgb565('theme.keys.defaultText'),
      keyFilledBg: this.getRgb565('theme.keys.filledBg'),
      keyFilledText: this.getRgb565('theme.keys.filledText'),
      keyFocusedBorder: this.getRgb565('theme.keys.focusedBorder'),
      keyEditingBorder: this.getRgb565('theme.keys.editingBorder'),
      palette: this.theme.palette.map(c => toRgb565(c)),
      btnLineVisible: this.buttonStyle.lineVisible,
      btnLineColor: toRgb565(this.buttonStyle.lineColor),
      btnFillVisible: this.buttonStyle.fillVisible,
      btnFillColor: toRgb565(this.buttonStyle.fillColor),
      titleLineVisible: this.titleStyle.lineVisible,
      titleLineColor: toRgb565(this.titleStyle.lineColor),
      titleFillVisible: this.titleStyle.fillVisible,
      titleFillColor: toRgb565(this.titleStyle.fillColor),
      titleFontColor: toRgb565(this.titleStyle.fontColor),
      btnMainFontColor: toRgb565(this.fontColors.main),
      btnSubFontColor: toRgb565(this.fontColors.sub),
      autoFontReverse: this.fontColors.autoReverse,
      rowBgColors: this.rowBgColors.map(c => toRgb565(c)),
      individualButtonBg: Object.fromEntries(
        Object.entries(this.individualButtonBg).map(([k, v]) => [k, toRgb565(v)])
      ),
      gradientType: gradMap[this.gradient.type] ?? 0,
      gradientStart: toRgb565(this.gradient.startColor),
      gradientEnd: toRgb565(this.gradient.endColor),
      debugVisible: this.debug.visible
    };
  }

  toJSON() {
    return {
      id: this.id,
      name: this.name,
      description: this.description,
      version: this.version,
      isBuiltin: this.isBuiltin,
      theme: this.theme,
      fonts: this.fonts,
      buttonStyle: this.buttonStyle,
      titleStyle: this.titleStyle,
      rowBgColors: this.rowBgColors,
      individualButtonBg: this.individualButtonBg,
      gradient: this.gradient,
      debug: this.debug,
      fontColors: this.fontColors
    };
  }
}

/**
 * Built-in skins for MK20 hardware terminal.
 */
export const BUILTIN_MK20_SKINS = Object.freeze([
  new DeviceSkin({
    id: 'slate-dark',
    name: 'Slate Dark',
    description: 'Modern slate dark theme with cyan and emerald accents',
    isBuiltin: true,
    theme: {
      top: {
        background: '#080d1a',
        card: '#121b2a',
        border: '#1e293b',
        text: '#e2e8f0',
        textDim: '#64748b',
        accent: '#06b6d4',
        status: { idle: '#64748b', active: '#06b6d4', success: '#10b981', warning: '#f59e0b', error: '#f43f5e' }
      },
      keys: {
        defaultBg: '#121b2a',
        defaultBorder: '#1e293b',
        defaultText: '#e2e8f0',
        defaultSubText: '#64748b',
        filledBg: '#06b6d4',
        filledText: '#ffffff',
        focusedBorder: '#06b6d4',
        focusedBg: '#164850',
        editingBorder: '#f59e0b',
        editingBg: '#2c1b05',
        disabledBg: '#0b101b',
        disabledText: '#334155'
      },
      palette: ['#e2e8f0', '#06b6d4', '#10b981', '#f59e0b', '#f43f5e', '#64748b']
    },
    fonts: {
      korean: 'D2Coding',
      english: 'D2Coding',
      sizeTitle: 10,
      sizeMain: 14,
      sizeSub: 10,
      sizeDebug: 9
    },
    buttonStyle: {
      lineVisible: true,
      lineColor: '#1e293b',
      fillVisible: true,
      fillColor: '#121b2a'
    },
    titleStyle: {
      lineVisible: false,
      lineColor: '#313131',
      fillVisible: true,
      fillColor: '#19283a',
      fontColor: '#06b6d4'
    },
    rowBgColors: ['#10203a', '#101829', '#081421', '#080c19'],
    individualButtonBg: {
      4: '#3a1021',
      16: '#083563',
      20: '#082d21'
    },
    gradient: {
      type: 'vertical',
      startColor: '#192031',
      endColor: '#081019'
    },
    debug: {
      visible: false
    },
    fontColors: {
      title: '#06b6d4',
      main: '#e2e8f0',
      sub: '#64748b',
      autoReverse: true
    }
  }),
  new DeviceSkin({
    id: 'matrix-emerald',
    name: 'Matrix Emerald',
    description: 'Cyber terminal with vivid phosphor green palette',
    isBuiltin: true,
    theme: {
      top: {
        background: '#040d06',
        card: '#081c0e',
        border: '#15803d',
        text: '#4ade80',
        textDim: '#166534',
        accent: '#22c55e',
        status: { idle: '#166534', active: '#4ade80', success: '#22c55e', warning: '#eab308', error: '#ef4444' }
      },
      keys: {
        defaultBg: '#081c0e',
        defaultBorder: '#15803d',
        defaultText: '#4ade80',
        defaultSubText: '#166534',
        filledBg: '#22c55e',
        filledText: '#040d06',
        focusedBorder: '#4ade80',
        focusedBg: '#0d3818',
        editingBorder: '#86efac',
        editingBg: '#052e16',
        disabledBg: '#020603',
        disabledText: '#14532d'
      },
      palette: ['#4ade80', '#22c55e', '#86efac', '#a3e635', '#ef4444', '#166534']
    },
    fonts: {
      korean: 'D2Coding',
      english: 'D2Coding',
      sizeTitle: 10,
      sizeMain: 14,
      sizeSub: 10,
      sizeDebug: 9
    },
    buttonStyle: {
      lineVisible: true,
      lineColor: '#15803d',
      fillVisible: true,
      fillColor: '#081c0e'
    },
    titleStyle: {
      lineVisible: false,
      lineColor: '#15803d',
      fillVisible: true,
      fillColor: '#003d10',
      fontColor: '#4ade80'
    },
    rowBgColors: ['#082810', '#081c08', '#081008', '#080808'],
    individualButtonBg: {
      4: '#290c08',
      20: '#105921'
    },
    gradient: {
      type: 'vertical',
      startColor: '#083510',
      endColor: '#001000'
    },
    debug: {
      visible: false
    },
    fontColors: {
      title: '#4ade80',
      main: '#4ade80',
      sub: '#166534',
      autoReverse: true
    }
  }),
  new DeviceSkin({
    id: 'cyberpunk-neon',
    name: 'Cyberpunk Neon',
    description: 'Synthwave neon with magenta, violet, and electric cyan',
    isBuiltin: true,
    theme: {
      top: {
        background: '#0f081d',
        card: '#1d1137',
        border: '#7c3aed',
        text: '#f43f5e',
        textDim: '#a855f7',
        accent: '#06b6d4',
        status: { idle: '#a855f7', active: '#06b6d4', success: '#10b981', warning: '#facc15', error: '#f43f5e' }
      },
      keys: {
        defaultBg: '#1d1137',
        defaultBorder: '#7c3aed',
        defaultText: '#f43f5e',
        defaultSubText: '#a855f7',
        filledBg: '#f43f5e',
        filledText: '#ffffff',
        focusedBorder: '#06b6d4',
        focusedBg: '#3b1c71',
        editingBorder: '#facc15',
        editingBg: '#4c1d95',
        disabledBg: '#0a0414',
        disabledText: '#581c87'
      },
      palette: ['#f8fafc', '#06b6d4', '#10b981', '#facc15', '#f43f5e', '#a855f7']
    },
    fonts: {
      korean: 'D2Coding',
      english: 'D2Coding',
      sizeTitle: 10,
      sizeMain: 14,
      sizeSub: 10,
      sizeDebug: 9
    },
    buttonStyle: {
      lineVisible: true,
      lineColor: '#7c3aed',
      fillVisible: true,
      fillColor: '#1d1137'
    },
    titleStyle: {
      lineVisible: false,
      lineColor: '#f43f5e',
      fillVisible: true,
      fillColor: '#291842',
      fontColor: '#06b6d4'
    },
    rowBgColors: ['#211842', '#191031', '#100c29', '#080419'],
    individualButtonBg: {
      4: '#5a1419',
      16: '#422063',
      20: '#084173'
    },
    gradient: {
      type: 'LTtoRB',
      startColor: '#292042',
      endColor: '#080c10'
    },
    debug: {
      visible: false
    },
    fontColors: {
      title: '#06b6d4',
      main: '#f43f5e',
      sub: '#a855f7',
      autoReverse: true
    }
  }),
  new DeviceSkin({
    id: 'amber-crt',
    name: 'Amber CRT',
    description: 'Warm retro amber monochrome phosphor CRT monitor',
    isBuiltin: true,
    theme: {
      top: {
        background: '#140c02',
        card: '#261604',
        border: '#78350f',
        text: '#f59e0b',
        textDim: '#92400e',
        accent: '#fbbf24',
        status: { idle: '#92400e', active: '#fbbf24', success: '#f59e0b', warning: '#d97706', error: '#dc2626' }
      },
      keys: {
        defaultBg: '#261604',
        defaultBorder: '#78350f',
        defaultText: '#f59e0b',
        defaultSubText: '#92400e',
        filledBg: '#f59e0b',
        filledText: '#140c02',
        focusedBorder: '#fbbf24',
        focusedBg: '#452708',
        editingBorder: '#fde68a',
        editingBg: '#3d1a04',
        disabledBg: '#0c0601',
        disabledText: '#78350f'
      },
      palette: ['#fef3c7', '#fbbf24', '#f59e0b', '#d97706', '#b45309', '#78350f']
    },
    fonts: {
      korean: 'D2Coding',
      english: 'D2Coding',
      sizeTitle: 10,
      sizeMain: 14,
      sizeSub: 10,
      sizeDebug: 9
    },
    buttonStyle: {
      lineVisible: true,
      lineColor: '#78350f',
      fillVisible: true,
      fillColor: '#261604'
    },
    titleStyle: {
      lineVisible: false,
      lineColor: '#b55108',
      fillVisible: true,
      fillColor: '#291800',
      fontColor: '#fbbf24'
    },
    rowBgColors: ['#292008', '#211400', '#190c00', '#100400'],
    individualButtonBg: {
      4: '#311c08',
      20: '#313910'
    },
    gradient: {
      type: 'vertical',
      startColor: '#292408',
      endColor: '#100800'
    },
    debug: {
      visible: false
    },
    fontColors: {
      title: '#fbbf24',
      main: '#f59e0b',
      sub: '#92400e',
      autoReverse: true
    }
  }),
  new DeviceSkin({
    id: 'high-contrast',
    name: 'High Contrast',
    description: 'Pure black and white monochrome for extreme readability',
    isBuiltin: true,
    theme: {
      top: {
        background: '#000000',
        card: '#121212',
        border: '#ffffff',
        text: '#ffffff',
        textDim: '#a0a0a0',
        accent: '#ffffff',
        status: { idle: '#a0a0a0', active: '#ffffff', success: '#ffffff', warning: '#ffffff', error: '#ffffff' }
      },
      keys: {
        defaultBg: '#000000',
        defaultBorder: '#ffffff',
        defaultText: '#ffffff',
        defaultSubText: '#a0a0a0',
        filledBg: '#ffffff',
        filledText: '#000000',
        focusedBorder: '#ffffff',
        focusedBg: '#333333',
        editingBorder: '#ffffff',
        editingBg: '#222222',
        disabledBg: '#000000',
        disabledText: '#555555'
      },
      palette: ['#ffffff', '#ffffff', '#ffffff', '#ffffff', '#ffffff', '#888888']
    },
    fonts: {
      korean: 'D2Coding',
      english: 'D2Coding',
      sizeTitle: 10,
      sizeMain: 14,
      sizeSub: 10,
      sizeDebug: 9
    },
    buttonStyle: {
      lineVisible: true,
      lineColor: '#ffffff',
      fillVisible: true,
      fillColor: '#000000'
    },
    titleStyle: {
      lineVisible: true,
      lineColor: '#ffffff',
      fillVisible: true,
      fillColor: '#1a1a1a',
      fontColor: '#ffffff'
    },
    rowBgColors: ['#000000', '#000000', '#000000', '#000000'],
    individualButtonBg: {},
    gradient: {
      type: 'none',
      startColor: '#000000',
      endColor: '#000000'
    },
    debug: {
      visible: false
    },
    fontColors: {
      title: '#ffffff',
      main: '#ffffff',
      sub: '#a0a0a0',
      autoReverse: true
    }
  })
]);

/**
 * Manages multiple skins for a single device plugin instance.
 * Allows runtime registration, switching, cycling, and theme token generation.
 */
export class DeviceSkinManager {
  #skins = new Map();
  #activeSkinId = 'slate-dark';

  constructor(initialSkins = BUILTIN_MK20_SKINS, defaultSkinId = 'slate-dark') {
    for (const skin of initialSkins) {
      const instance = skin instanceof DeviceSkin ? skin : new DeviceSkin(skin);
      this.#skins.set(instance.id, instance);
    }
    if (!this.#skins.has(defaultSkinId)) {
      const first = this.#skins.keys().next().value;
      if (!first) throw new Mk20Fault('no_skins_available');
      this.#activeSkinId = first;
    } else {
      this.#activeSkinId = defaultSkinId;
    }
  }

  get activeSkinId() {
    return this.#activeSkinId;
  }

  getActiveSkin() {
    return this.#skins.get(this.#activeSkinId);
  }

  getSkin(id) {
    if (!id) return this.getActiveSkin();
    return this.#skins.get(id);
  }

  hasSkin(id) {
    return this.#skins.has(id);
  }

  listSkins() {
    return Array.from(this.#skins.values()).map(skin => ({
      id: skin.id,
      name: skin.name,
      description: skin.description,
      isBuiltin: skin.isBuiltin,
      active: skin.id === this.#activeSkinId
    }));
  }

  setActiveSkin(id) {
    if (!this.#skins.has(id)) {
      throw new Mk20Fault('skin_not_found');
    }
    const previous = this.#activeSkinId;
    this.#activeSkinId = id;
    return {
      previousSkinId: previous,
      activeSkin: this.getActiveSkin()
    };
  }

  cycleSkin(delta = 1) {
    const list = Array.from(this.#skins.keys());
    if (list.length === 0) throw new Mk20Fault('no_skins_available');
    const curIdx = list.indexOf(this.#activeSkinId);
    const nextIdx = (curIdx + delta % list.length + list.length) % list.length;
    return this.setActiveSkin(list[nextIdx]);
  }

  registerSkin(definition) {
    const skin = definition instanceof DeviceSkin ? definition : new DeviceSkin(definition);
    if (this.#skins.has(skin.id)) {
      const existing = this.#skins.get(skin.id);
      if (existing.isBuiltin) throw new Mk20Fault('builtin_skin_protected');
    }
    this.#skins.set(skin.id, skin);
    return skin;
  }

  unregisterSkin(id) {
    if (!this.#skins.has(id)) return false;
    const skin = this.#skins.get(id);
    if (skin.isBuiltin) throw new Mk20Fault('builtin_skin_protected');
    if (this.#activeSkinId === id) throw new Mk20Fault('active_skin_in_use');
    return this.#skins.delete(id);
  }

  applyToPreview(view) {
    if (!view || typeof view !== 'object') return view;
    const active = this.getActiveSkin();
    return {
      ...view,
      skinId: active.id,
      skinName: active.name,
      themeTokens: active.toWireTokens()
    };
  }
}

/**
 * Extends the existing MK20 UX (20-key matrix + top HUD + rotary knobs)
 * to operate skin browsing, cycling, and theme switching directly from the device.
 */
export class Mk20SkinUxController {
  constructor(options = {}) {
    this.skinManager = options.skinManager || new DeviceSkinManager();
    this.viewMode = 'session'; // 'session' | 'settings'
    this.activeEditor = 'none'; // 'none' | 'skin'
    this.focusedSkinIdx = 0;
    this.syncFocusedSkin();
  }

  syncFocusedSkin() {
    const skins = this.skinManager.listSkins();
    const idx = skins.findIndex(s => s.id === this.skinManager.activeSkinId);
    this.focusedSkinIdx = idx >= 0 ? idx : 0;
  }

  handleInput(event) {
    if (!event || typeof event !== 'object') return { handled: false };

    // Button presses
    if (event.kind === 'button' && event.pressed) {
      const kid = parseInt(String(event.button).replace('key-', ''), 10);
      if (isNaN(kid)) return { handled: false };

      // In Session Mode
      if (this.viewMode === 'session') {
        if (kid === 2) {
          // Key 2 is SYSTEM / Settings
          this.viewMode = 'settings';
          this.activeEditor = 'none';
          this.syncFocusedSkin();
          return { handled: true, action: 'open_settings', view: this.renderView() };
        }
        return { handled: false };
      }

      // In Settings Mode
      if (this.viewMode === 'settings') {
        if (kid === 17) {
          // Key 17: Close settings -> return to session
          this.viewMode = 'session';
          this.activeEditor = 'none';
          return { handled: true, action: 'close_settings', view: this.renderView() };
        }
        if (kid === 18) {
          // Key 18: SKIN / Cycle -> cycle to next skin
          const res = this.skinManager.cycleSkin(+1);
          this.syncFocusedSkin();
          return { handled: true, action: 'cycle_skin', skin: res.activeSkin, view: this.renderView() };
        }
        if (kid === 14) {
          // Key 14: THEME / Browse -> toggle skin editor
          this.activeEditor = this.activeEditor === 'skin' ? 'none' : 'skin';
          this.syncFocusedSkin();
          return { handled: true, action: 'toggle_skin_editor', view: this.renderView() };
        }
        if (kid === 10) {
          // Key 10: RESET -> set back to slate-dark
          const res = this.skinManager.setActiveSkin('slate-dark');
          this.syncFocusedSkin();
          return { handled: true, action: 'reset_skin', skin: res.activeSkin, view: this.renderView() };
        }

        // Choice keys on Row 2: K19, K15, K11, K7, K3
        const choiceKeys = [19, 15, 11, 7, 3];
        const choiceOffset = choiceKeys.indexOf(kid);
        if (choiceOffset >= 0) {
          const skins = this.skinManager.listSkins();
          if (choiceOffset < skins.length) {
            const res = this.skinManager.setActiveSkin(skins[choiceOffset].id);
            this.focusedSkinIdx = choiceOffset;
            return { handled: true, action: 'select_skin', skin: res.activeSkin, view: this.renderView() };
          }
        }
        return { handled: true, action: 'noop' };
      }
    }

    // Rotary Knobs
    if (event.kind === 'knob-turn' && event.knob === 'left') {
      if (this.viewMode === 'settings') {
        const skins = this.skinManager.listSkins();
        const delta = event.delta > 0 ? 1 : -1;
        this.focusedSkinIdx = (this.focusedSkinIdx + delta + skins.length) % skins.length;
        return { handled: true, action: 'scroll_skin', focusedSkin: skins[this.focusedSkinIdx], view: this.renderView() };
      }
    }

    if (event.kind === 'knob-click' && event.knob === 'left') {
      if (this.viewMode === 'settings') {
        const skins = this.skinManager.listSkins();
        const target = skins[this.focusedSkinIdx];
        if (target) {
          const res = this.skinManager.setActiveSkin(target.id);
          return { handled: true, action: 'apply_skin', skin: res.activeSkin, view: this.renderView() };
        }
      }
    }

    return { handled: false };
  }

  renderView() {
    const active = this.skinManager.getActiveSkin();
    const skins = this.skinManager.listSkins();

    if (this.viewMode === 'settings') {
      const focused = skins[this.focusedSkinIdx] || active;
      const keys = [];

      // Row 0: K17 (Close), K18 (Cycle Skin), K14 (Browse Themes), K10 (Reset)
      for (let kid = 1; kid <= 20; kid++) {
        if (kid === 17) {
          keys.push({ keyId: 17, labelTop: 'SETTINGS', labelMain: 'Close', labelSub: 'Return', isFilled: true, isDisabled: false });
        } else if (kid === 18) {
          keys.push({ keyId: 18, labelTop: 'SKIN', labelMain: 'Cycle', labelSub: 'Next', isFilled: false, isDisabled: false });
        } else if (kid === 14) {
          keys.push({ keyId: 14, labelTop: 'THEME', labelMain: 'Browse', labelSub: `${skins.length} Skins`, isFilled: this.activeEditor === 'skin', isDisabled: false });
        } else if (kid === 10) {
          keys.push({ keyId: 10, labelTop: 'RESET', labelMain: 'Default', labelSub: 'Slate', isFilled: false, isDisabled: false });
        } else if ([19, 15, 11, 7, 3].includes(kid)) {
          // Row 2: 5 Skin Choice slots
          const choiceIdx = [19, 15, 11, 7, 3].indexOf(kid);
          if (choiceIdx < skins.length) {
            const sk = skins[choiceIdx];
            const isCurrentActive = sk.id === active.id;
            const isCursorFocused = choiceIdx === this.focusedSkinIdx;
            keys.push({
              keyId: kid,
              labelTop: isCurrentActive ? 'ACTIVE' : `SKIN ${choiceIdx + 1}`,
              labelMain: sk.name.split(' ')[0],
              labelSub: sk.name.split(' ')[1] || '',
              isFilled: isCurrentActive,
              isFocused: isCursorFocused,
              isDisabled: false
            });
          } else {
            keys.push({ keyId: kid, labelTop: '', labelMain: '', isFilled: false, isDisabled: true });
          }
        } else {
          keys.push({ keyId: kid, labelTop: '', labelMain: '', isFilled: false, isDisabled: true });
        }
      }

      return {
        mode: 'settings',
        skinId: active.id,
        skinName: active.name,
        title: 'Settings',
        subtitle: `Theme: ${active.name} | Knob: browse`,
        lines: [
          `Active Skin: ${active.name} (${active.id})`,
          `Focused: ${focused.name} — ${focused.description}`,
          `Knob: Scroll Skins | Click: Select`,
          `K18: Cycle | K14: Browse | K17: Close`
        ],
        scroll: 0,
        totalLines: 4,
        volume: 45,
        muted: false,
        keys: keys.map(k => ({
          id: k.keyId,
          top: k.labelTop || '',
          main: k.labelMain || '',
          sub: k.labelSub || '',
          flags: (k.isFilled ? 1 : 0) | (k.isFocused ? 4 : 0) | (k.isDisabled ? 8 : 0)
        }))
      };
    }

    // Default Session View
    const keys = [];
    for (let kid = 1; kid <= 20; kid++) {
      if (kid === 2) {
        keys.push({ id: 2, top: 'SYSTEM', main: 'Settings', sub: active.name.split(' ')[0], flags: 0 });
      } else {
        keys.push({ id: kid, top: '', main: `K${kid}`, sub: '', flags: 0 });
      }
    }

    return {
      mode: 'session',
      skinId: active.id,
      skinName: active.name,
      title: 'Snowball Control',
      subtitle: `Skin: ${active.name}`,
      lines: ['Session Online', 'Press K2 (Settings) to change Theme'],
      scroll: 0,
      totalLines: 2,
      volume: 45,
      muted: false,
      keys
    };
  }
}
