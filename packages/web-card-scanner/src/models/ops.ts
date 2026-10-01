/** Activations and shape helpers composed from TIE's own ops.
 *
 *  TIE's conv epilogue fuses SiLU only, so the MobileNet activations are
 *  built here from `clamp` and the scalar ops, which pass an HWC4-stored
 *  Value straight through (they touch no neighbouring element). The channel
 *  lanes HWC4 pads to a multiple of 4 hold 0, and both ReLU(0) and
 *  x·HardSigmoid(x) at x = 0 are 0, so the padding invariant survives.
 *  HardSigmoid alone maps 0 to 0.5, which is why it is only ever applied to
 *  a squeeze-excite gate whose product with a 0-padded map is 0 again. */

import {
  add,
  clamp,
  mean,
  mul,
  reshape,
  toChw,
  toHwc4,
  upsample2d,
  type Value,
} from '@tie/core';

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

/** clamp(x/6 + 1/2, 0, 1) — torch.nn.Hardsigmoid. */
export function hardsigmoid(x: Value): Value {
  return clamp(add(mul(x, 1 / 6), 0.5), 0, 1);
}

/** x · hardsigmoid(x) — torch.nn.Hardswish. */
export function hardswish(x: Value): Value {
  return mul(x, hardsigmoid(x));
}

/** Global average pool: HWC4 [C,H,W] → HWC4 [C,1,1], ready for the 1×1
 *  convs of a head or a squeeze-excite gate. TIE has no pooling reduction of
 *  its own, so the whole map goes row-major for the mean (in f32, so a
 *  thousand-pixel reduction does not accumulate in half precision) and the
 *  [C,1] result returns to the layout. */
export function globalAvgPool(x: Value): Value {
  const [c, h, w] = dims3(x);
  const flat = reshape(toChw(x, 'f32'), [c, h * w]);
  return toHwc4(reshape(mean(flat), [c, 1, 1]));
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
