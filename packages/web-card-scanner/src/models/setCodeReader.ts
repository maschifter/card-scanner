/** SetCodeReader (Yu-Gi-Oh! set codes): a conv-only CTC reader, transcribed
 *  from apps/web-example/tools/export-set-code-reader.py, which rebuilds the
 *  CDN manifest's model from its PyTorch weights. The exporter folds
 *  BatchNorm into each trunk conv.
 *
 *  Input [1,96,256]: the gray search box, already normalized per crop by
 *  `normalizeSetCodeCrop` (the deployed model does that as its first op;
 *  here it runs on the CPU, where the crop is made). Output [64,38]: logits
 *  per step, class 0 the CTC blank; softmax them on the CPU.
 *
 *  TIE pools square windows only, so the height-only (2,1) pools and the
 *  final max over the 6 remaining rows are composed from slices
 *  (`maxRowGroups`). */

import {
  maxPool2d,
  nn,
  reshape,
  slice,
  toChw,
  toHwc4,
  transpose,
  type Value,
} from '../runntime.ts';
import { conv2d } from './layers.ts';
import { dims3, maximum, relu } from './ops.ts';

const { Conv1d, Module } = nn;

/** Output class i + 1 is SET_CODE_ALPHABET[i]; class 0 is the CTC blank. The
 *  manifest's set_code_reader labels, in class order. */
export const SET_CODE_ALPHABET = '-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ';

/** The classes as a read reports them (SetCodeInfo.classNames): the CTC
 *  blank, then the alphabet. */
export const SET_CODE_CLASS_NAMES: readonly string[] = [
  'blank',
  ...SET_CODE_ALPHABET,
];

/** The model's per-crop normalization: (x - mean) / (std + eps). */
const NORM_EPS = 0.02;

/** Max over each group of `k` consecutive rows: HWC4 [C,H,W] → row-major
 *  f32 [C,H/k,W]. In row-major CHW, reshaping to [C·H/k, k·W] lays each
 *  group's rows side by side, so the k column slices are the rows to max. */
function maxRowGroups(x: Value, k: number): Value {
  const [c, h, w] = dims3(x);
  const groups = reshape(toChw(x, 'f32'), [(c * h) / k, k * w]);
  let out = slice(groups, 1, 0, w);
  for (let i = 1; i < k; i++) {
    out = maximum(out, slice(groups, 1, i * w, (i + 1) * w));
  }
  return reshape(out, [c, h / k, w]);
}

/** Field names follow the checkpoint keys `trunk.body.<i>.weight`; the
 *  missing indices are the pools. */
class Body extends Module {
  readonly 0 = conv2d({ in: 1, out: 20, k: 3, activation: 'relu' });
  readonly 2 = conv2d({ in: 20, out: 40, k: 3, activation: 'relu' });
  readonly 4 = conv2d({ in: 40, out: 60, k: 3, activation: 'relu' });
  readonly 5 = conv2d({ in: 60, out: 60, k: 3, activation: 'relu' });
  readonly 7 = conv2d({ in: 60, out: 80, k: 3, activation: 'relu' });
  readonly 8 = conv2d({ in: 80, out: 80, k: 3, activation: 'relu' });
  readonly 10 = conv2d({ in: 80, out: 100, k: 3, activation: 'relu' });

  forward(x: Value): Value {
    const pool = (y: Value) =>
      maxPool2d(y, { kernelSize: 2, stride: 2, padding: 0 });
    const tallPool = (y: Value) => toHwc4(maxRowGroups(y, 2));
    let y = toHwc4(x);
    y = pool(this[0].forward(y)); // 48 x 128
    y = pool(this[2].forward(y)); // 24 x 64
    y = tallPool(this[5].forward(this[4].forward(y))); // 12 x 64
    y = tallPool(this[8].forward(this[7].forward(y))); // 6 x 64
    return this[10].forward(y);
  }
}

class Trunk extends Module {
  readonly body = new Body();
}

class Head extends Module {
  readonly 0 = new Conv1d(100, 100, 3, { padding: 1 });
  readonly 2 = new Conv1d(100, 38, 1);

  /** [T, 100] time-major → [T, 38]. The ReLU stays a separate op: fused into
   *  Conv1d (activation: 'relu') it fails parity (0.97 off) on vendor/runntime
   *  4f9443b. */
  forward(x: Value): Value {
    return this[2].forward(relu(this[0].forward(x)));
  }
}

export class SetCodeReaderModel extends Module {
  readonly trunk = new Trunk();
  readonly head = new Head();

  constructor() {
    super();
    // The conv stack runs hwc4+f16 (f16 biases); the 1-D head stays f32.
    this.trunk.half();
  }

  forward(x: Value): Value {
    const features = this.trunk.body.forward(x); // [100, 6, 64]
    const [c, , w] = dims3(features);
    // Strongest of the 6 rows per column, then time-major for the head.
    const columns = reshape(maxRowGroups(features, 6), [c, w]);
    return this.head.forward(transpose(columns));
  }
}

/** Normalizes a gray 0-1 crop in place as the model's first op does:
 *  zero mean, unit (biased) deviation plus NORM_EPS. */
export function normalizeSetCodeCrop<T extends Float32Array>(gray: T): T {
  let sum = 0;
  for (const v of gray) sum += v;
  const mean = sum / gray.length;
  let sq = 0;
  for (const v of gray) sq += (v - mean) * (v - mean);
  const scale = 1 / (Math.sqrt(sq / gray.length) + NORM_EPS);
  for (let i = 0; i < gray.length; i++) gray[i] = (gray[i] - mean) * scale;
  return gray;
}
