/** SetSymbolRecognitionModel: MobileNetV3-Small-style backbone + 128-d
 *  embedding head, transcribed from the deployed ONNX graph (BN pre-folded).
 *  Input [3,96,96] ImageNet-normalized; output [1,128], L2-normalize on the
 *  CPU after readback. */

import { add, nn, toHwc4, type Value } from '@tie/core';
import {
  conv2d,
  ConvUnit,
  SqueezeExcite,
  type Conv,
  type ConvAct,
  type ConvSpec,
  type Lin,
  type Stage,
} from './layers.ts';
import { globalAvgPool, hardswish, relu, toRow } from './ops.ts';

const { Conv2d, Linear, Module, ModuleList } = nn;

/** Apply the block's activation (TIE fuses SiLU only, so these ride after
 *  the conv — see ops.ts). */
function activate(x: Value, act: ConvAct): Value {
  return act === 'relu' ? relu(x) : hardswish(x);
}

interface IrSpec {
  pw: ConvSpec;
  dwK: number;
  dwStride?: number;
  seReduced?: number;
  pwl: ConvSpec;
  act: ConvAct;
}

/** timm InvertedResidual: conv_pw(+act) → conv_dw(+act) → [se] → conv_pwl,
 *  residual when shape-preserving. */
class InvertedResidual extends Module {
  readonly conv_pw: Conv;
  readonly conv_dw: Conv;
  readonly se?: SqueezeExcite;
  readonly conv_pwl: Conv;
  private readonly residual: boolean;
  private readonly act: ConvAct;

  constructor(spec: IrSpec) {
    super();
    const mid = spec.pw.out;
    const stride = spec.dwStride ?? 1;
    this.act = spec.act;
    this.conv_pw = conv2d(spec.pw);
    this.conv_dw = conv2d({
      in: mid,
      out: mid,
      k: spec.dwK,
      stride,
      groups: mid,
    });
    if (spec.seReduced) this.se = new SqueezeExcite(mid, spec.seReduced);
    this.conv_pwl = conv2d(spec.pwl);
    this.residual = spec.pw.in === spec.pwl.out && stride === 1;
  }

  forward(x: Value): Value {
    let y = activate(this.conv_pw.forward(x), this.act);
    y = activate(this.conv_dw.forward(y), this.act);
    if (this.se) y = this.se.forward(y);
    y = this.conv_pwl.forward(y);
    return this.residual ? add(y, x) : y;
  }
}

/** timm DepthwiseSeparableConv (first mnv3 block):
 *  conv_dw(+ReLU) → se → conv_pw. */
class DsConv extends Module {
  readonly conv_dw = conv2d({ in: 16, out: 16, k: 3, stride: 2, groups: 16 });
  readonly se = new SqueezeExcite(16, 8);
  readonly conv_pw = conv2d({ in: 16, out: 16, k: 1 });

  forward(x: Value): Value {
    const y = this.se.forward(relu(this.conv_dw.forward(x)));
    return this.conv_pw.forward(y);
  }
}

/** blocks.5.0: 1×1 conv + hardswish (timm ConvBnAct → `.conv` key). */
class ConvHs extends ConvUnit {
  override forward(x: Value): Value {
    return hardswish(super.forward(x));
  }
}

interface IrOpts {
  stride?: number;
  se?: number;
}

function ir(
  cin: number,
  mid: number,
  cout: number,
  dwK: number,
  act: ConvAct,
  opts: IrOpts = {},
) {
  return new InvertedResidual({
    pw: { in: cin, out: mid, k: 1 },
    dwK,
    dwStride: opts.stride,
    seReduced: opts.se,
    pwl: { in: mid, out: cout, k: 1 },
    act,
  });
}

class Backbone extends Module {
  readonly conv_stem = new Conv2d(3, 16, {
    kernelSize: 3,
    stride: 2,
    padding: 1,
  });
  readonly blocks = new ModuleList<nn.AnyModule>([
    new ModuleList([new DsConv()]),
    new ModuleList([
      ir(16, 72, 24, 3, 'relu', { stride: 2 }),
      ir(24, 88, 24, 3, 'relu'),
    ]),
    new ModuleList([
      ir(24, 96, 32, 5, 'hardswish', { stride: 2, se: 24 }),
      ir(32, 192, 32, 5, 'hardswish', { se: 48 }),
      ir(32, 192, 32, 5, 'hardswish', { se: 48 }),
    ]),
    new ModuleList([
      ir(32, 96, 40, 5, 'hardswish', { se: 24 }),
      ir(40, 120, 40, 5, 'hardswish', { se: 32 }),
    ]),
    new ModuleList([
      ir(40, 240, 72, 5, 'hardswish', { stride: 2, se: 64 }),
      ir(72, 432, 72, 5, 'hardswish', { se: 112 }),
      ir(72, 432, 72, 5, 'hardswish', { se: 112 }),
    ]),
    new ModuleList([new ConvHs({ in: 72, out: 432, k: 1 })]),
  ]);
  readonly conv_head = new Conv2d(432, 1024, { kernelSize: 1 });

  forward(x: Value): Value {
    let y = hardswish(this.conv_stem.forward(toHwc4(x)));
    for (const stage of this.blocks.items) {
      y = (stage as Stage).forward(y);
    }
    y = globalAvgPool(y);
    return hardswish(this.conv_head.forward(y));
  }
}

export class SetSymbolRecognitionModel extends Module {
  readonly backbone = new Backbone();
  readonly embedding = new ModuleList<Lin>([new Linear(1024, 128)]);

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
