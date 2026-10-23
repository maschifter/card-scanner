/** CardLanguageModel (print-language classifier), transcribed from the deployed
 *  ONNX graph. Input [3,512,256] in [-1,1]; output [12,32,1] logits per row. */

import {
  add,
  cat,
  derivedTensor,
  type LazyStateDict,
  mean,
  mul,
  nn,
  pad2d,
  reshape,
  resizeBilinear2d,
  rsqrt,
  sigmoid,
  toChw,
  toHwc4,
  type Value,
} from '../runntime.ts';
import { conv2d, type Conv, type ConvSpec } from './layers.ts';
import { dims3, relu } from './ops.ts';

const { Conv2d, Module, ModuleList } = nn;

/** Class order of the first output channels; on-text and zh-traditional follow. */
export const LANGUAGE_CODES = [
  'de',
  'en',
  'es',
  'fr',
  'it',
  'ja',
  'ko',
  'pt',
  'ru',
  'zh',
] as const;

const CHANNELS = 160;

/** [height, width]. */
type Axes = [number, number];

interface PerAxisSpec extends Omit<ConvSpec, 'k' | 'stride'> {
  k: Axes;
  stride?: Axes;
  dilation?: Axes;
}

const perAxis = (f: (axis: 0 | 1) => number): Axes => [f(0), f(1)];

const paddingOf = ({ k, dilation = [1, 1] }: PerAxisSpec) =>
  perAxis((a) => dilation[a] * Math.floor((k[a] - 1) / 2));

/** Odd kernels with stride 2 get a zero tap: the resize needs an odd length. */
const kernelOf = ({ k, stride = [1, 1], dilation = [1, 1] }: PerAxisSpec) =>
  perAxis(
    (a) =>
      dilation[a] * (k[a] - 1) +
      1 +
      (stride[a] === 2 && k[a] % 2 === 1 ? 1 : 0),
  );

/** torch Conv2d with stride, padding and dilation per axis. ruNNtime's conv2d
 *  takes one stride and padding and no dilation, so pad2d, zero taps and the
 *  default alignCorners resize give the same sums. */
class PerAxisConv extends Conv2d {
  constructor(private readonly spec: PerAxisSpec) {
    super(spec.in, spec.out, {
      kernelSize: kernelOf(spec),
      padding: Math.min(...paddingOf(spec)),
      groups: spec.groups ?? 1,
      activation: spec.activation,
    });
  }

  override forward(x: Value): Value {
    const [padH, padW] = paddingOf(this.spec);
    const p = Math.min(padH, padW);
    const y = super.forward(
      padH === padW ? x : pad2d(x, { padH: padH - p, padW: padW - p }),
    );
    const [strideH, strideW] = this.spec.stride ?? [1, 1];
    if (strideH === 1 && strideW === 1) return y;
    const [, h, w] = dims3(y);
    return resizeBilinear2d(y, {
      outH: (h - 1) / strideH + 1,
      outW: (w - 1) / strideW + 1,
    });
  }

  override transformStateDict(sd: LazyStateDict, prefix: string): void {
    const key = `${prefix}.weight`;
    const weight = sd.tensors.get(key);
    if (!weight) return;
    const [kH, kW] = this.spec.k;
    const [dilH, dilW] = this.spec.dilation ?? [1, 1];
    const [rows, cols] = kernelOf(this.spec);
    const filters = weight.shape.reduce((a, b) => a * b, 1) / (kH * kW);
    const spread = async () => {
      const to = new Float32Array(filters * rows * cols);
      (await weight.f32()).forEach((v, i) => {
        const f = Math.floor(i / (kH * kW));
        const ky = Math.floor(i / kW) % kH;
        const kx = i % kW;
        to[(f * rows + ky * dilH) * cols + kx * dilW] = v;
      });
      return to;
    };
    sd.tensors.set(
      key,
      derivedTensor([filters, rows, cols], spread, weight.byteLength),
    );
  }
}

class Block extends Module {
  readonly depthwise: Conv;
  readonly pointwise = conv2d({ in: CHANNELS, out: CHANNELS, k: 1 });

  constructor(k: Axes, dilation: Axes) {
    super();
    this.depthwise = new PerAxisConv({
      in: CHANNELS,
      out: CHANNELS,
      k,
      dilation,
      groups: CHANNELS,
    });
  }

  forward(x: Value): Value {
    return relu(add(x, this.pointwise.forward(this.depthwise.forward(x))));
  }
}

const alongRow = (dilation: number) => new Block([1, 3], [1, dilation]);
const acrossRows = (dilation: number) => new Block([3, 1], [dilation, 1]);

class Downsample extends Module {
  readonly conv = new PerAxisConv({
    in: CHANNELS,
    out: CHANNELS,
    k: [1, 3],
    stride: [1, 2],
    groups: CHANNELS,
    activation: 'relu',
  });

  forward(x: Value): Value {
    return this.conv.forward(x);
  }
}

/** Per row (Σ x·a + mean x) / (Σ a + 1), a = σ(attention): mean(x·b) / mean(b)
 *  with b = a + 1/W. ruNNtime has no divide, so 1/m is rsqrt(m)². */
function poolRows(x: Value, attention: Value): Value {
  const [c, h, w] = dims3(x);
  const b = add(sigmoid(reshape(toChw(attention, 'f32'), [h, w])), 1 / w);
  const inv = rsqrt(mean(b));
  const weights = reshape(mul(b, mul(inv, inv)), [1, h * w]);
  const weighted = mul(reshape(toChw(x, 'f32'), [c, h * w]), weights);
  return toHwc4(reshape(mean(reshape(weighted, [c * h, w])), [c, h, 1]));
}

class Net extends Module {
  readonly stem = new ModuleList([
    new ModuleList([
      conv2d({ in: 3, out: 24, k: 3, stride: 2, activation: 'relu' }),
    ]),
    new ModuleList([
      new PerAxisConv({
        in: 24,
        out: 48,
        k: [3, 3],
        stride: [2, 1],
        activation: 'relu',
      }),
    ]),
    new ModuleList([
      new PerAxisConv({
        in: 48,
        out: 48,
        k: [3, 3],
        stride: [2, 1],
        groups: 48,
        activation: 'relu',
      }),
    ]),
    new ModuleList([
      conv2d({ in: 48, out: CHANNELS, k: 1, activation: 'relu' }),
    ]),
    new PerAxisConv({
      in: CHANNELS,
      out: CHANNELS,
      k: [4, 1],
      stride: [2, 1],
      groups: CHANNELS,
      activation: 'relu',
    }),
  ]);
  readonly body = new ModuleList([
    alongRow(1),
    alongRow(2),
    new Downsample(),
    alongRow(2),
    alongRow(4),
    alongRow(8),
    alongRow(1),
  ]);
  readonly position_head = conv2d({ in: CHANNELS, out: 1, k: 1 });
  readonly rows_mix = new ModuleList([acrossRows(1), acrossRows(2)]);
  readonly lang_head = conv2d({
    in: CHANNELS,
    out: LANGUAGE_CODES.length,
    k: 1,
  });
  readonly on_text_head = conv2d({ in: CHANNELS, out: 1, k: 1 });
  readonly script_head = conv2d({ in: CHANNELS, out: 1, k: 1 });

  forward(x: Value): Value {
    let y = toHwc4(x);
    y = this.stem.forward(y);
    y = this.body.forward(y);
    const rows = this.rows_mix.forward(
      poolRows(y, this.position_head.forward(y)),
    );
    const heads = [this.lang_head, this.on_text_head, this.script_head];
    return cat(
      heads.map((head) => toChw(head.forward(rows), 'f32')),
      0,
    );
  }
}

/** The checkpoint keys start with `model.`. */
export class CardLanguageModel extends Module {
  readonly model = new Net();

  constructor() {
    super();
    this.model.half();
  }

  forward(x: Value): Value {
    return this.model.forward(x);
  }
}
