/** Shared building blocks for the timm-style embedder backbones. Field names
 *  mirror the checkpoint keys exactly (see
 *  apps/web-example/tools/export-web-models.py — the exporter lifts BN-folded
 *  weights out of the ONNX graph, so every conv here carries a bias and no
 *  BatchNorm module exists at runtime).
 *
 *  TIE's conv kernel fuses SiLU and nothing else, so activations are applied
 *  after the conv from `ops.ts` rather than through the `act` option. */

import { mul, nn, type Value } from '@tie/core';
import {
  broadcastGate,
  dims3,
  globalAvgPool,
  hardsigmoid,
  relu,
} from './ops.ts';

const { Conv2d, Module } = nn;

export type Conv = InstanceType<typeof Conv2d>;
export type Lin = InstanceType<typeof nn.Linear>;
/** One backbone stage: a ModuleList of blocks. */
export type Stage = InstanceType<typeof nn.ModuleList<nn.AnyModule>>;

/** Block activations in these backbones. HardSigmoid appears only inside a
 *  squeeze-excite gate, never as a block's own activation. */
export type ConvAct = 'relu' | 'hardswish';

export interface ConvSpec {
  in: number;
  out: number;
  k: number;
  stride?: number;
  groups?: number;
}

export function conv2d(spec: ConvSpec): Conv {
  return new Conv2d(spec.in, spec.out, {
    kernelSize: spec.k,
    stride: spec.stride ?? 1,
    padding: Math.floor(spec.k / 2),
    groups: spec.groups ?? 1,
  });
}

/** timm ConvNormAct: a `conv` child so keys read `<block>.conv.weight`. */
export class ConvUnit extends Module {
  readonly conv: Conv;

  constructor(spec: ConvSpec) {
    super();
    this.conv = conv2d(spec);
  }

  forward(x: Value): Value {
    return this.conv.forward(x);
  }
}

/** timm SqueezeExcite: GAP → conv_reduce(+ReLU) → conv_expand(+hardsigmoid),
 *  gating the input per channel. Both 1×1 convs run on the pooled [C,1,1]
 *  map, and the gate is upsampled back to the feature map's size so the
 *  multiply is a plain elementwise op in the HWC4 layout. */
export class SqueezeExcite extends Module {
  readonly conv_reduce: Conv;
  readonly conv_expand: Conv;

  constructor(channels: number, reduced: number) {
    super();
    this.conv_reduce = conv2d({ in: channels, out: reduced, k: 1 });
    this.conv_expand = conv2d({ in: reduced, out: channels, k: 1 });
  }

  forward(x: Value): Value {
    const [, h, w] = dims3(x);
    if (h !== w) {
      throw new Error(
        `SqueezeExcite: the gate is broadcast by an upsample, which needs a square map (got ${h}x${w})`,
      );
    }
    const pooled = globalAvgPool(x);
    const squeezed = relu(this.conv_reduce.forward(pooled));
    const gate = hardsigmoid(this.conv_expand.forward(squeezed));
    return mul(x, broadcastGate(gate, h));
  }
}
