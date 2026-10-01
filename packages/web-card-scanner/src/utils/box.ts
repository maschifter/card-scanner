/** Axis-aligned box helpers — utils/BoxGeometry.h. */

import type { Quad } from './geometry.ts';

export interface Box {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

const EPS = 1e-6;

export function area(b: Box): number {
  return Math.max(0, b.x2 - b.x1) * Math.max(0, b.y2 - b.y1);
}

function intersectionArea(a: Box, b: Box): number {
  const w = Math.min(a.x2, b.x2) - Math.max(a.x1, b.x1);
  const h = Math.min(a.y2, b.y2) - Math.max(a.y1, b.y1);
  return Math.max(0, w) * Math.max(0, h);
}

export function iou(a: Box, b: Box): number {
  const inter = intersectionArea(a, b);
  return inter / (area(a) + area(b) - inter + EPS);
}

/** Fraction of `inner`'s own area covered by `outer`. */
export function containedFraction(inner: Box, outer: Box): number {
  return intersectionArea(inner, outer) / (area(inner) + EPS);
}

/** How many times the two aspect ratios differ, regardless of direction. */
export function aspectRatioMismatch(a: Box, b: Box): number {
  const ar = (b: Box) => Math.max(1, b.x2 - b.x1) / Math.max(1, b.y2 - b.y1);
  const aa = ar(a);
  const ab = ar(b);
  return Math.max(aa / ab, ab / aa);
}

export function bounds(quad: Quad): Box {
  return {
    x1: Math.min(...quad.map((p) => p.x)),
    y1: Math.min(...quad.map((p) => p.y)),
    x2: Math.max(...quad.map((p) => p.x)),
    y2: Math.max(...quad.map((p) => p.y)),
  };
}
