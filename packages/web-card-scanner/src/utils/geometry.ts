/** Card-quad extraction from a YOLO-seg instance mask, mirroring
 *  YoloSegmentationModel.cpp: largest component → convex hull → approxPolyDP
 *  over an epsilon ladder → minAreaRect fallback → order → orient. All in
 *  mask-grid coordinates; callers scale the quad into frame pixels. */

import {
  MIN_POINT_DISTANCE,
  MIN_QUAD_AREA,
  ORIENT_Y_TOLERANCE,
  QUAD_EPSILON_FRACS,
  TOPMOST_TIE_TOLERANCE,
} from '../constants.ts';
import { argMax, argMin } from './array.ts';

export type Pt = { x: number; y: number };
export type Quad = [Pt, Pt, Pt, Pt];

/** Row-major 3×3 matrix. */
export type Homography = [
  number,
  number,
  number,
  number,
  number,
  number,
  number,
  number,
  number,
];

const dist = (a: Pt, b: Pt) => Math.hypot(a.x - b.x, a.y - b.y);

/** Cells of the largest 4-connected component of mask >= threshold. */
function largestComponent(
  mask: Uint8Array,
  w: number,
  h: number,
  threshold: number,
): Pt[] {
  const cells = w * h;
  const seen = new Uint8Array(cells);
  const inside = (i: number) => i >= 0 && i < cells && mask[i] >= threshold;

  let best: Pt[] = [];
  for (let start = 0; start < cells; start++) {
    if (seen[start] || !inside(start)) continue;

    const component: Pt[] = [];
    const stack = [start];
    seen[start] = 1;
    while (stack.length) {
      const i = stack.pop() as number;
      const x = i % w;
      const y = (i - x) / w;
      component.push({ x, y });

      const neighbours = [i - w, i + w];
      if (x > 0) neighbours.push(i - 1);
      if (x < w - 1) neighbours.push(i + 1);
      for (const j of neighbours) {
        if (!inside(j) || seen[j]) continue;
        seen[j] = 1;
        stack.push(j);
      }
    }
    if (component.length > best.length) best = component;
  }
  return best;
}

/** Andrew monotone-chain convex hull, counter-clockwise. */
export function convexHull(pts: Pt[]): Pt[] {
  const sorted = [...pts].sort((a, b) => a.x - b.x || a.y - b.y);
  if (sorted.length <= 2) return sorted;

  const cross = (o: Pt, a: Pt, b: Pt) =>
    (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);

  const chain = (points: Pt[]) => {
    const out: Pt[] = [];
    for (const p of points) {
      while (
        out.length >= 2 &&
        cross(out[out.length - 2], out[out.length - 1], p) <= 0
      ) {
        out.pop();
      }
      out.push(p);
    }
    out.pop(); // the last point starts the other chain
    return out;
  };

  return [...chain(sorted), ...chain(sorted.reverse())];
}

function perimeter(poly: Pt[]): number {
  let total = 0;
  for (let i = 0; i < poly.length; i++) {
    total += dist(poly[i], poly[(i + 1) % poly.length]);
  }
  return total;
}

function pointLineDist(p: Pt, a: Pt, b: Pt): number {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len = Math.hypot(dx, dy);
  if (len === 0) return dist(p, a);
  return Math.abs(dy * p.x - dx * p.y + b.x * a.y - b.y * a.x) / len;
}

/** Closed-polygon Douglas-Peucker anchored at the two farthest-apart
 *  vertices; not a port of cv::approxPolyDP, so a vertex can land one or two
 *  cells from the native one. */
export function approxPolyDP(poly: Pt[], eps: number): Pt[] {
  if (poly.length <= 4) return [...poly];

  // The two anchors: the farthest-apart pair of vertices.
  let ai = 0;
  let bi = 0;
  let farthest = -1;
  for (let i = 0; i < poly.length; i++) {
    for (let j = i + 1; j < poly.length; j++) {
      const d = dist(poly[i], poly[j]);
      if (d > farthest) {
        farthest = d;
        ai = i;
        bi = j;
      }
    }
  }

  const simplify = (pts: Pt[]): Pt[] => {
    if (pts.length < 3) return [...pts];
    const first = pts[0];
    const last = pts[pts.length - 1];

    let splitAt = -1;
    let maxDist = eps;
    for (let i = 1; i < pts.length - 1; i++) {
      const d = pointLineDist(pts[i], first, last);
      if (d > maxDist) {
        maxDist = d;
        splitAt = i;
      }
    }
    if (splitAt < 0) return [first, last];

    const head = simplify(pts.slice(0, splitAt + 1));
    const tail = simplify(pts.slice(splitAt));
    return [...head.slice(0, -1), ...tail];
  };

  const arc1 = poly.slice(ai, bi + 1);
  const arc2 = [...poly.slice(bi), ...poly.slice(0, ai + 1)];
  return [...simplify(arc1).slice(0, -1), ...simplify(arc2).slice(0, -1)];
}

/** Minimum-area enclosing rectangle of a convex hull (rotating edges). */
export function minAreaRect(hull: Pt[]): Quad {
  let best: Quad | null = null;
  let bestArea = Infinity;

  for (let i = 0; i < hull.length; i++) {
    const a = hull[i];
    const b = hull[(i + 1) % hull.length];
    const angle = Math.atan2(b.y - a.y, b.x - a.x);
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);

    // Extent of the hull in the frame aligned with this edge.
    let minU = Infinity;
    let maxU = -Infinity;
    let minV = Infinity;
    let maxV = -Infinity;
    for (const p of hull) {
      const u = p.x * cos + p.y * sin;
      const v = -p.x * sin + p.y * cos;
      minU = Math.min(minU, u);
      maxU = Math.max(maxU, u);
      minV = Math.min(minV, v);
      maxV = Math.max(maxV, v);
    }

    const area = (maxU - minU) * (maxV - minV);
    if (area >= bestArea) continue;
    bestArea = area;
    const corner = (u: number, v: number): Pt => ({
      x: u * cos - v * sin,
      y: u * sin + v * cos,
    });
    best = [
      corner(minU, minV),
      corner(maxU, minV),
      corner(maxU, maxV),
      corner(minU, maxV),
    ];
  }
  return best as Quad;
}

function quadValid(q: Pt[]): boolean {
  for (let i = 0; i < q.length; i++) {
    for (let j = i + 1; j < q.length; j++) {
      if (dist(q[i], q[j]) < MIN_POINT_DISTANCE) return false;
    }
  }

  let twiceArea = 0;
  for (let i = 0; i < q.length; i++) {
    const a = q[i];
    const b = q[(i + 1) % q.length];
    twiceArea += a.x * b.y - b.x * a.y;
  }
  return Math.abs(twiceArea / 2) >= MIN_QUAD_AREA;
}

/** TL, TR, BR, BL by the (x+y)/(y−x) extremes — YoloSegmentationModel.cpp
 *  orderQuad. */
export function orderQuad(q: Quad): Quad {
  const sum = (p: Pt) => p.x + p.y;
  const diff = (p: Pt) => p.y - p.x;
  return [argMin(q, sum), argMin(q, diff), argMax(q, sum), argMax(q, diff)];
}

export interface OrientedQuad {
  /** TL, TR, BR, BL with the shorter edge on top (portrait card). */
  quad: Quad;
  /** True when the topmost edge was the long edge — the card lies sideways
   *  and a matched embedding may need the 180° retry. */
  sideways: boolean;
}

/** YoloSegmentationModel.cpp orientQuad: the edge whose midpoint is highest
 *  (leftmost when level within ORIENT_Y_TOLERANCE) is the top; its endpoints
 *  become TL/TR by x, the far corners BR/BL by closeness to TR. A card's top
 *  edge is its short edge, so when that top is longer than the side the quad
 *  is rotated a quarter turn so the dewarp output stays portrait. */
export function orientQuad(q: Quad): OrientedQuad {
  const midpoints = q.map((a, i) => {
    const b = q[(i + 1) % 4];
    return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
  });

  const highest = Math.min(...midpoints.map((m) => m.y));
  const candidates = [0, 1, 2, 3].filter(
    (i) => midpoints[i].y <= highest + TOPMOST_TIE_TOLERANCE,
  );
  candidates.sort((i, j) => {
    const dy = midpoints[i].y - midpoints[j].y;
    if (Math.abs(dy) < ORIENT_Y_TOLERANCE)
      return midpoints[i].x - midpoints[j].x;
    return dy;
  });

  const top = candidates[0];
  const a = q[top];
  const b = q[(top + 1) % 4];
  const c = q[(top + 2) % 4];
  const d = q[(top + 3) % 4];
  const [tl, tr] = a.x <= b.x ? [a, b] : [b, a];
  const [br, bl] = dist(c, tr) <= dist(d, tr) ? [c, d] : [d, c];

  const topIsLongEdge = dist(tr, tl) > dist(br, tr);
  if (topIsLongEdge) return { quad: [tr, br, bl, tl], sideways: true };
  return { quad: [tl, tr, br, bl], sideways: false };
}

/** Mask grid → oriented card quad in mask-grid coordinates, or null when no
 *  usable component exists. `threshold` matches the decoder's soft-coverage
 *  encoding (128 ≡ logit 0 ≡ the C++ 0.5 mask cut). */
export function maskToQuad(
  mask: Uint8Array,
  w: number,
  h: number,
  threshold = 128,
): OrientedQuad | null {
  const component = largestComponent(mask, w, h, threshold);
  if (component.length < 4) return null;

  const hull = convexHull(component);
  if (hull.length < 3) return null;

  const ladder = fitQuad(hull);
  if (ladder) return orientQuad(ladder);

  const rect = orderQuad(minAreaRect(hull));
  return quadValid(rect) ? orientQuad(rect) : null;
}

/** The first epsilon on the ladder that reduces the hull to a valid ordered
 *  quad. */
function fitQuad(hull: Pt[]): Quad | null {
  const hullPerimeter = perimeter(hull);
  for (const frac of QUAD_EPSILON_FRACS) {
    const approx = approxPolyDP(hull, frac * hullPerimeter);
    if (approx.length !== 4) continue;
    const ordered = orderQuad(approx as Quad);
    if (quadValid(ordered)) return ordered;
  }
  return null;
}

/** Homography mapping the corners of a wOut×hOut rect onto `quad`
 *  (TL,TR,BR,BL) — cv::getPerspectiveTransform(dstRect, quad). */
export function rectToQuadHomography(
  quad: Quad,
  wOut: number,
  hOut: number,
): Homography {
  const rect: Pt[] = [
    { x: 0, y: 0 },
    { x: wOut - 1, y: 0 },
    { x: wOut - 1, y: hOut - 1 },
    { x: 0, y: hOut - 1 },
  ];

  // The standard 8×8 DLT system A·h = b for h = [h00 .. h21].
  const a: number[][] = [];
  const b: number[] = [];
  for (let i = 0; i < 4; i++) {
    const s = rect[i];
    const d = quad[i];
    a.push([s.x, s.y, 1, 0, 0, 0, -s.x * d.x, -s.y * d.x]);
    b.push(d.x);
    a.push([0, 0, 0, s.x, s.y, 1, -s.x * d.y, -s.y * d.y]);
    b.push(d.y);
  }

  // Gaussian elimination with partial pivoting.
  for (let col = 0; col < 8; col++) {
    let pivot = col;
    for (let r = col + 1; r < 8; r++) {
      if (Math.abs(a[r][col]) > Math.abs(a[pivot][col])) pivot = r;
    }
    [a[col], a[pivot]] = [a[pivot], a[col]];
    [b[col], b[pivot]] = [b[pivot], b[col]];

    const lead = a[col][col];
    for (let r = col + 1; r < 8; r++) {
      const f = a[r][col] / lead;
      for (let c = col; c < 8; c++) a[r][c] -= f * a[col][c];
      b[r] -= f * b[col];
    }
  }

  // Back substitution.
  const h = new Array<number>(8);
  for (let r = 7; r >= 0; r--) {
    let s = b[r];
    for (let c = r + 1; c < 8; c++) s -= a[r][c] * h[c];
    h[r] = s / a[r][r];
  }
  return [...h, 1] as Homography;
}
