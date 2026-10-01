import { describe, expect, it } from 'vitest';
import { decodeCards, dropGroupBoxes } from '../utils/decode.ts';

const NC = 2;
const NM = 1;

/** One 2×2 level at stride 8 with a proto grid at stride 4 (4×4). */
function level(cells: { box: number[]; logits: number[]; coeff: number }[]) {
  const hw = 4;
  const data = new Float32Array((4 + NC + NM) * hw).fill(-10);
  cells.forEach((c, i) => {
    c.box.forEach((v, k) => (data[k * hw + i] = v));
    c.logits.forEach((v, k) => (data[(4 + k) * hw + i] = v));
    data[(4 + NC) * hw + i] = c.coeff;
  });
  return { data, h: 2, w: 2, stride: 8 };
}

const proto = { data: new Float32Array(16).fill(5), h: 4, w: 4 };

describe('decodeCards', () => {
  it('keeps cells above the threshold with every class score', () => {
    const dets = decodeCards(
      [
        level([
          { box: [1, 1, 1, 1], logits: [3, -3], coeff: 1 },
          { box: [1, 1, 1, 1], logits: [-1, 0], coeff: 1 },
        ]),
      ],
      proto,
      { numClasses: NC, confThreshold: 0.6, maskPadding: 4, numMasks: NM },
    );
    expect(dets).toHaveLength(1);
    const [d] = dets;
    expect(d!.classId).toBe(0);
    expect(d!.score).toBeCloseTo(0.9526, 3);
    expect([...d!.classScores]).toHaveLength(NC);
    // Cell 0 anchor (0.5, 0.5) ± 1 cell at stride 8 → [-4, 12].
    expect(d!.x1).toBe(-4);
    expect(d!.x2).toBe(12);
    // Mask cropped to the (padded) box, all cells on (proto logit 5).
    expect(d!.maskW).toBeGreaterThan(0);
    expect(Math.min(...d!.mask)).toBeGreaterThan(250);
  });

  it('caps the number of detections', () => {
    const cells = Array.from({ length: 4 }, () => ({
      box: [1, 1, 1, 1],
      logits: [3, 0],
      coeff: 0,
    }));
    const dets = decodeCards([level(cells)], proto, {
      numClasses: NC,
      confThreshold: 0.5,
      maskPadding: 0,
      maxDet: 2,
      numMasks: NM,
    });
    expect(dets).toHaveLength(2);
  });

  it('pads the mask crop by maskPadding input pixels', () => {
    // Cell 3 anchor (1.5, 1.5) ± 0.5 cell at stride 8 → box [8, 16]; the
    // proto grid is 4×4 at stride 4.
    const off = { box: [1, 1, 1, 1], logits: [-9, -9], coeff: 0 };
    const card = { box: [0.5, 0.5, 0.5, 0.5], logits: [3, 0], coeff: 0 };
    const crop = (maskPadding: number) => {
      const [d] = decodeCards([level([off, off, off, card])], proto, {
        numClasses: NC,
        confThreshold: 0.5,
        maskPadding,
        numMasks: NM,
      });
      return { x: d!.maskX, w: d!.maskW };
    };
    expect(crop(0)).toEqual({ x: 2, w: 2 });
    expect(crop(4)).toEqual({ x: 1, w: 3 });
  });
});

describe('dropGroupBoxes', () => {
  const card = (x: number, y: number) => ({
    x1: x,
    y1: y,
    x2: x + 40,
    y2: y + 60,
  });

  it('drops a box that wraps two cards', () => {
    const table = { x1: 0, y1: 0, x2: 200, y2: 100 };
    const kept = dropGroupBoxes([table, card(10, 10), card(100, 10)]);
    expect(kept).toEqual([card(10, 10), card(100, 10)]);
  });

  it('drops a much larger box of a different aspect around one card', () => {
    const page = { x1: 0, y1: 0, x2: 200, y2: 80 };
    expect(dropGroupBoxes([page, card(10, 10)])).toEqual([card(10, 10)]);
  });

  it('keeps a same-aspect container (a sleeve) and unrelated boxes', () => {
    const sleeve = { x1: 8, y1: 8, x2: 52, y2: 72 };
    expect(dropGroupBoxes([sleeve, card(10, 10)])).toHaveLength(2);
    expect(dropGroupBoxes([card(0, 0), card(100, 100)])).toHaveLength(2);
  });
});
