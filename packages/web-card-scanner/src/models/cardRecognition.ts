/** CardRecognitionModel: MobileNetV4-Conv-Small backbone + 256-d embedding
 *  head, transcribed from the deployed ONNX graph (BN pre-folded by the
 *  exporter, embedding BatchNorm1d folded into the Linear at export time).
 *  Input [3,224,224] ImageNet-normalized; output [1,256], L2-normalize on
 *  the CPU after readback. */

import { add, nn, toHwc4, type Value } from '@tie/core';
import { ConvUnit, type ConvSpec, type Lin, type Stage } from './layers.ts';
import { globalAvgPool, relu, toRow } from './ops.ts';

const { Conv2d, Linear, Module, ModuleList } = nn;

interface UibSpec {
  dwStart?: ConvSpec;
  pwExp: ConvSpec;
  dwMid?: ConvSpec;
  pwProj: ConvSpec;
  residual: boolean;
}

/** MobileNetV4 universal inverted bottleneck. Optional depthwise convs at the
 *  start and middle; only the expand and mid convs are activated (ReLU). */
class Uib extends Module {
  readonly dw_start?: ConvUnit;
  readonly pw_exp: ConvUnit;
  readonly dw_mid?: ConvUnit;
  readonly pw_proj: ConvUnit;

  constructor(private readonly spec: UibSpec) {
    super();
    if (spec.dwStart) this.dw_start = new ConvUnit(spec.dwStart);
    this.pw_exp = new ConvUnit(spec.pwExp);
    if (spec.dwMid) this.dw_mid = new ConvUnit(spec.dwMid);
    this.pw_proj = new ConvUnit(spec.pwProj);
  }

  forward(x: Value): Value {
    let y = this.dw_start ? this.dw_start.forward(x) : x;
    y = relu(this.pw_exp.forward(y));
    if (this.dw_mid) y = relu(this.dw_mid.forward(y));
    y = this.pw_proj.forward(y);
    return this.spec.residual ? add(y, x) : y;
  }
}

/** Conv + ReLU stage (timm ConvBnAct → `.conv` key). */
class ConvRelu extends ConvUnit {
  override forward(x: Value): Value {
    return relu(super.forward(x));
  }
}

/** Depthwise k×k conv keeping `c` channels. */
const dw = (c: number, k: number, stride = 1): ConvSpec => ({
  in: c,
  out: c,
  k,
  stride,
  groups: c,
});

/** Pointwise 1×1 conv. */
const pw = (cin: number, cout: number): ConvSpec => ({
  in: cin,
  out: cout,
  k: 1,
});

interface UibOpts {
  dwStartK?: number;
  dwMidK?: number;
  midStride?: number;
}

function uib(cin: number, exp: number, cout: number, opts: UibOpts = {}) {
  const midStride = opts.midStride ?? 1;
  return new Uib({
    dwStart: opts.dwStartK ? dw(cin, opts.dwStartK) : undefined,
    pwExp: pw(cin, exp),
    dwMid: opts.dwMidK ? dw(exp, opts.dwMidK, midStride) : undefined,
    pwProj: pw(exp, cout),
    residual: cin === cout && midStride === 1,
  });
}

class Backbone extends Module {
  readonly conv_stem = new Conv2d(3, 32, {
    kernelSize: 3,
    stride: 2,
    padding: 1,
  });
  readonly blocks = new ModuleList<nn.AnyModule>([
    new ModuleList([
      new ConvRelu({ in: 32, out: 32, k: 3, stride: 2 }),
      new ConvRelu({ in: 32, out: 32, k: 1 }),
    ]),
    new ModuleList([
      new ConvRelu({ in: 32, out: 96, k: 3, stride: 2 }),
      new ConvRelu({ in: 96, out: 64, k: 1 }),
    ]),
    new ModuleList([
      uib(64, 192, 96, { dwStartK: 5, dwMidK: 5, midStride: 2 }),
      uib(96, 192, 96, { dwMidK: 3 }),
      uib(96, 192, 96, { dwMidK: 3 }),
      uib(96, 192, 96, { dwMidK: 3 }),
      uib(96, 192, 96, { dwMidK: 3 }),
      uib(96, 384, 96, { dwStartK: 3 }),
    ]),
    new ModuleList([
      uib(96, 576, 128, { dwStartK: 3, dwMidK: 3, midStride: 2 }),
      uib(128, 512, 128, { dwStartK: 5, dwMidK: 5 }),
      uib(128, 512, 128, { dwMidK: 5 }),
      uib(128, 384, 128, { dwMidK: 5 }),
      uib(128, 512, 128, { dwMidK: 3 }),
      uib(128, 512, 128, { dwMidK: 3 }),
    ]),
    new ModuleList([new ConvRelu({ in: 128, out: 960, k: 1 })]),
  ]);
  readonly conv_head = new Conv2d(960, 1280, { kernelSize: 1 });

  forward(x: Value): Value {
    let y = relu(this.conv_stem.forward(toHwc4(x)));
    for (const stage of this.blocks.items) {
      y = (stage as Stage).forward(y);
    }
    y = globalAvgPool(y);
    return relu(this.conv_head.forward(y));
  }
}

export class CardRecognitionModel extends Module {
  readonly backbone = new Backbone();
  readonly embedding = new ModuleList<Lin>([new Linear(1280, 256)]);

  constructor() {
    super();
    // The conv stack runs hwc4+f16 (f16 biases); the Linear head stays f32
    // on the f32 row toRow() produces.
    this.backbone.half();
  }

  forward(x: Value): Value {
    const features = toRow(this.backbone.forward(x));
    return this.embedding.items[0].forward(features);
  }
}
