/** Activations and shape helpers composed from TIE's own ops.
 *
 *  TIE refuses ops that fill HWC4 padding lanes, so each step maps 0 to 0. */

import {
  add,
  avgPool2d,
  clamp,
  mul,
  reshape,
  toChw,
  upsample2d,
  type Value,
} from '../runntime.ts';

/** [C, H, W] of a feature map. */
export function dims3(x: Value): [number, number, number] {
  const dims = x.shape.dims ?? [];
  if (dims.length !== 3) {
    throw new Error(`expected a [C,H,W] value, got [${dims.join(',')}]`);
  }
  return [dims[0], dims[1], dims[2]];
}

/** max(x, 0) — torch.relu. Safe on HWC4 and on row-major storage. */
export function relu(x: Value): Value {
  return clamp(x, 0, Infinity);
}

/** y · torch.nn.Hardsigmoid(x). */
export function timesHardsigmoid(y: Value, x: Value): Value {
  return add(mul(y, clamp(mul(x, 1 / 6), -0.5, 0.5)), mul(y, 0.5));
}

/** x · hardsigmoid(x) — torch.nn.Hardswish. */
export function hardswish(x: Value): Value {
  return timesHardsigmoid(x, x);
}

/** Global average pool of a square map: HWC4 [C,H,H] → HWC4 [C,1,1]. */
export function globalAvgPool(x: Value): Value {
  const [, h] = dims3(x);
  return avgPool2d(x, { kernelSize: h });
}

/** Broadcast an HWC4 [C,1,1] gate up to [C,size,size] so it can multiply a
 *  feature map elementwise. TIE refuses row/column broadcasting against HWC4
 *  (the storage interleaves channels, so a row-major broadcast would mix
 *  pixels), but a nearest-neighbour upsample of a one-pixel map copies that
 *  pixel to every position, which is the same thing. Square maps only, which
 *  is what every backbone here produces. */
export function broadcastGate(gate: Value, size: number): Value {
  return size === 1 ? gate : upsample2d(gate, { scale: size });
}

/** HWC4 [C,1,1] feature column → row-major f32 [1,C] row for a Linear head. */
export function toRow(x: Value): Value {
  const [c] = dims3(x);
  return reshape(toChw(x, 'f32'), [1, c]);
}
