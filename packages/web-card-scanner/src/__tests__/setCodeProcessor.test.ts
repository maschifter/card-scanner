import { describe, expect, it } from 'vitest';
import {
  decode,
  grayCrop,
  isBetter,
  looksLikeCode,
  textBoxTop,
} from '../core/setCodeProcessor.ts';
import {
  normalizeSetCodeCrop,
  SET_CODE_ALPHABET,
} from '../models/setCodeReader.ts';

const CLASSES = SET_CODE_ALPHABET.length + 1;

/** A plain RGBA image: ImageData's fields without the DOM class. */
function image(
  width: number,
  height: number,
  pixel: (x: number, y: number) => number,
): ImageData {
  const data = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const v = pixel(x, y);
      data.set([v, v, v, 255], (y * width + x) * 4);
    }
  }
  return { width, height, data } as unknown as ImageData;
}

/** Logits picking `path[t]` (a character, or '' for the blank) per step. */
function logits(path: string[]): Float32Array {
  const out = new Float32Array(path.length * CLASSES);
  path.forEach((c, t) => {
    out[t * CLASSES + (c ? SET_CODE_ALPHABET.indexOf(c) + 1 : 0)] = 10;
  });
  return out;
}

describe('looksLikeCode', () => {
  it('accepts set codes', () => {
    for (const code of ['YGLD-ENA22', 'ME-10', '15AY-JPB15', 'RDS-EN034']) {
      expect(looksLikeCode(code)).toBe(true);
    }
  });
  it('rejects text that is not a code', () => {
    for (const text of ['', 'YGLD', 'Y-EN022', 'YGLD-EN', 'A-B-C1', 'PCR11']) {
      expect(looksLikeCode(text)).toBe(false);
    }
  });
});

describe('isBetter', () => {
  const read = (text: string, confidence: number) => ({ text, confidence });
  it('ranks code-shaped, then any text, then empty, then confidence', () => {
    expect(isBetter(read('YGLD-ENA22', 0.6), read('YGLD', 0.99))).toBe(true);
    expect(isBetter(read('-EN0986', 0.88), read('', 0.99))).toBe(true);
    expect(isBetter(read('', 0.99), read('S4-E', 0.84))).toBe(false);
    expect(isBetter(read('YGLD-ENA22', 0.9), read('RDS-EN034', 0.8))).toBe(
      true,
    );
  });
});

describe('decode', () => {
  it('merges repeats, drops blanks and finds where the text sits', () => {
    const path = ['', '', 'M', 'M', '', 'E', '-', '', '1', '1', '', '1', '0'];
    const read = decode(logits(path), CLASSES);
    expect(read.text).toBe('ME-110');
    expect(read.first).toBe(2);
    expect(read.last).toBe(12);
    expect(read.steps).toBe(path.length);
    expect(read.confidence).toBeGreaterThan(0.99);
    const row = read.probs.subarray(0, CLASSES);
    expect(row.reduce((a, b) => a + b, 0)).toBeCloseTo(1, 5);
    expect(row[0]).toBeGreaterThan(0.99);
  });
});

describe('textBoxTop', () => {
  it('finds a long horizontal border in the band', () => {
    // A white card with a dark 3-row border at 70% of the height.
    const card = image(400, 640, (_, y) => (y >= 447 && y < 450 ? 0 : 255));
    expect(textBoxTop(card)).toBeCloseTo(448.5 / 640, 2);
  });
  it('takes the upper line of a double border', () => {
    // A shorter line 9 rows (1.4%) above the strongest one.
    const card = image(400, 640, (x, y) =>
      (y >= 438 && y < 441 && x < 280) || (y >= 447 && y < 450) ? 0 : 255,
    );
    expect(textBoxTop(card)).toBeCloseTo(439 / 640, 2);
  });
  it('ignores short strokes', () => {
    const card = image(400, 640, (x, y) =>
      y >= 447 && y < 450 && x < 60 ? 0 : 255,
    );
    expect(textBoxTop(card)).toBe(-1);
  });
});

describe('grayCrop', () => {
  it('matches cv::resize(INTER_LINEAR) and cvtColor(RGB2GRAY) exactly', () => {
    // Pseudo-random pixels from an LCG; the expected numbers come from
    // OpenCV 4.13 (cv2) run on the same image.
    const width = 120;
    const height = 60;
    const data = new Uint8ClampedArray(width * height * 4);
    let seed = 12345;
    for (let i = 0; i < data.length; i++) {
      seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
      data[i] = (seed >>> 16) & 255;
    }
    const card = { width, height, data } as unknown as ImageData;
    const crop = grayCrop(card, { x: 3, y: 2, w: 110, h: 55 });
    const gray = Array.from(crop, (v) => Math.round(v * 255));
    let sum = 0;
    let check = 0;
    gray.forEach((v, i) => {
      sum += v;
      check = (check + v * (i + 1)) >>> 0;
    });
    expect(gray.slice(0, 5)).toEqual([97, 90, 69, 49, 82]);
    expect(sum).toBe(3138007);
    expect(check).toBe(4096839451);
  });

  it('scales the crop to 256 x 96 and normalizes to zero mean', () => {
    const card = image(400, 640, (x) => (x < 300 ? 0 : 255));
    const crop = grayCrop(card, { x: 250, y: 100, w: 100, h: 50 });
    expect(crop.length).toBe(256 * 96);
    expect(crop[0]).toBe(0);
    expect(crop[255]).toBe(1);
    const normalized = normalizeSetCodeCrop(crop);
    const mean = normalized.reduce((a, b) => a + b, 0) / normalized.length;
    expect(Math.abs(mean)).toBeLessThan(1e-5);
  });
});
