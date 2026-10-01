import { describe, expect, it } from 'vitest';
import {
  maskToQuad,
  orderQuad,
  orientQuad,
  rectToQuadHomography,
  type Pt,
  type Quad,
} from '../utils/geometry.ts';
import { rotateQuad180 } from '../inference/gpuFrame.ts';

/** A w×h mask with the axis-aligned rect [x0,x1)×[y0,y1) filled. */
function rectMask(
  w: number,
  h: number,
  x0: number,
  y0: number,
  x1: number,
  y1: number,
) {
  const m = new Uint8Array(w * h);
  for (let y = y0; y < y1; y++)
    for (let x = x0; x < x1; x++) m[y * w + x] = 255;
  return m;
}

/** A w×w mask with a cardW×cardH rectangle rotated by `deg` about the
 *  centre, shifted by `offsetX`/`offsetY` cells. */
function rotatedRectMask(
  w: number,
  cardW: number,
  cardH: number,
  deg: number,
  offsetX: number,
  offsetY = 0,
) {
  const m = new Uint8Array(w * w);
  const c = Math.cos((deg * Math.PI) / 180);
  const s = Math.sin((deg * Math.PI) / 180);
  for (let y = 0; y < w; y++)
    for (let x = 0; x < w; x++) {
      const dx = x - w / 2 - offsetX;
      const dy = y - w / 2 - offsetY;
      const u = dx * c + dy * s;
      const v = -dx * s + dy * c;
      if (Math.abs(u) <= cardW / 2 && Math.abs(v) <= cardH / 2)
        m[y * w + x] = 255;
    }
  return m;
}

const edge = (a: Pt, b: Pt) => Math.hypot(a.x - b.x, a.y - b.y);
const near = (p: Pt, x: number, y: number, tol = 1.5) => {
  expect(Math.abs(p.x - x)).toBeLessThanOrEqual(tol);
  expect(Math.abs(p.y - y)).toBeLessThanOrEqual(tol);
};

describe('maskToQuad', () => {
  it('extracts a portrait rectangle as TL,TR,BR,BL and not sideways', () => {
    const r = maskToQuad(rectMask(60, 80, 10, 5, 40, 70), 60, 80)!;
    expect(r.sideways).toBe(false);
    near(r.quad[0], 10, 5);
    near(r.quad[1], 39, 5);
    near(r.quad[2], 39, 69);
    near(r.quad[3], 10, 69);
  });

  it('rotates a landscape rectangle to portrait and flags it sideways', () => {
    const r = maskToQuad(rectMask(80, 60, 5, 10, 70, 40), 80, 60)!;
    expect(r.sideways).toBe(true);
    // Top edge of the portrait output is the right edge of the landscape rect.
    near(r.quad[0], 69, 10);
    near(r.quad[1], 69, 39);
  });

  it('keeps four distinct corners on a card tilted 45°', () => {
    for (const [deg, offsetX, offsetY] of [
      [45, 0, 0],
      [45, 0, 0.5],
      [135, 0, 0],
    ]) {
      const m = rotatedRectMask(62, 30, 44, deg, offsetX, offsetY);
      const r = maskToQuad(m, 62, 62)!;
      expect(r.sideways).toBe(false);
      const q = r.quad;
      for (let i = 0; i < 4; i++)
        for (let j = i + 1; j < 4; j++)
          expect(edge(q[i], q[j])).toBeGreaterThan(20);
      expect(Math.abs(edge(q[0], q[1]) - 30)).toBeLessThanOrEqual(2);
      expect(Math.abs(edge(q[1], q[2]) - 44)).toBeLessThanOrEqual(2);
      const h = rectToQuadHomography(q, 403, 640);
      expect(h.every((v) => Number.isFinite(v))).toBe(true);
    }
  });

  it('returns null rather than a collapsed quad when every fit ties', () => {
    for (const [deg, offsetX] of [
      [45, 0.5],
      [135, 0.5],
    ]) {
      const m = rotatedRectMask(62, 30, 44, deg, offsetX);
      expect(maskToQuad(m, 62, 62)).toBeNull();
    }
  });

  it('returns null for an empty or degenerate mask', () => {
    expect(maskToQuad(new Uint8Array(16), 4, 4)).toBeNull();
    expect(maskToQuad(rectMask(4, 4, 0, 0, 1, 4), 4, 4)).toBeNull();
  });
});

describe('orientQuad', () => {
  it('keeps a tilted portrait card upright', () => {
    const q = orderQuad([
      { x: 0, y: 10 },
      { x: 50, y: 0 },
      { x: 60, y: 80 },
      { x: 10, y: 90 },
    ]);
    const r = orientQuad(q);
    expect(r.sideways).toBe(false);
    expect(r.quad).toEqual(q);
  });

  it('turns a landscape quad a quarter turn', () => {
    const r = orientQuad([
      { x: 0, y: 0 },
      { x: 80, y: 0 },
      { x: 80, y: 50 },
      { x: 0, y: 50 },
    ]);
    expect(r.sideways).toBe(true);
    expect(r.quad).toEqual([
      { x: 80, y: 0 },
      { x: 80, y: 50 },
      { x: 0, y: 50 },
      { x: 0, y: 0 },
    ]);
  });

  it('picks the top edge by midpoint height, not by corner order', () => {
    // A card tilted past 45°: the corner with the smallest x+y is not on
    // the topmost edge, so orderQuad's TL→TR edge is a side.
    const q: Quad = [
      { x: 0, y: 40 },
      { x: 30, y: 0 },
      { x: 100, y: 55 },
      { x: 70, y: 95 },
    ];
    const r = orientQuad(orderQuad(q));
    expect(r.quad[0]).toEqual({ x: 0, y: 40 });
    expect(r.quad[1]).toEqual({ x: 30, y: 0 });
    expect(r.sideways).toBe(false);
  });
});

describe('rectToQuadHomography', () => {
  it('maps the output rect corners onto the quad', () => {
    const quad: Quad = [
      { x: 12, y: 7 },
      { x: 110, y: 15 },
      { x: 120, y: 200 },
      { x: 5, y: 190 },
    ];
    const h = rectToQuadHomography(quad, 40, 64);
    const apply = (x: number, y: number): Pt => {
      const w = h[6] * x + h[7] * y + h[8];
      return {
        x: (h[0] * x + h[1] * y + h[2]) / w,
        y: (h[3] * x + h[4] * y + h[5]) / w,
      };
    };
    near(apply(0, 0), 12, 7, 1e-6);
    near(apply(39, 0), 110, 15, 1e-6);
    near(apply(39, 63), 120, 200, 1e-6);
    near(apply(0, 63), 5, 190, 1e-6);
  });
});

describe('rotateQuad180', () => {
  it('warps every output pixel from where the turned warp would', () => {
    const quad: Quad = [
      { x: 12, y: 7 },
      { x: 110, y: 15 },
      { x: 120, y: 200 },
      { x: 5, y: 190 },
    ];
    const [width, height] = [40, 64];
    const warp = (q: Quad) => {
      const h = rectToQuadHomography(q, width, height);
      return (x: number, y: number): Pt => {
        const d = h[6] * x + h[7] * y + h[8];
        return {
          x: (h[0] * x + h[1] * y + h[2]) / d,
          y: (h[3] * x + h[4] * y + h[5]) / d,
        };
      };
    };
    const upright = warp(quad);
    const turned = warp(rotateQuad180(quad));
    for (const [x, y] of [
      [0, 0],
      [13, 5],
      [39, 63],
      [21, 40],
    ]) {
      const p = upright(width - 1 - x, height - 1 - y);
      near(turned(x, y), p.x, p.y, 1e-6);
    }
  });
});
