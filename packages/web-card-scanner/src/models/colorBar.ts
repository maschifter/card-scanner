/** ColorBarModel (Flesh and Blood pitch-bar classifier): a small VGG-style
 *  conv stack + 3-layer MLP head, transcribed from the deployed ONNX graph.
 *  The head's BatchNorm1d layers are folded forward into the following Linear
 *  by the exporter, so `classifier` holds plain Linears at the original torch
 *  Sequential indices (1, 5, 9). Input [3,100,100] ImageNet-normalized;
 *  output [1,3] logits in bundle order [blue, red, yellow]. */

import { maxPool2d, nn, toHwc4, type Value } from '@tie/core';
import { conv2d, type Conv } from './layers.ts';
import { globalAvgPool, relu, toRow } from './ops.ts';

const { Linear, Module } = nn;

/** Softmax class order per the deployed bundle README. */
export const COLOR_BAR_LABELS = ['blue', 'red', 'yellow'] as const;

/** Fields are named after the torch Sequential indices so checkpoint keys
 *  (`features.0.weight`, ...) resolve without a key map. */
class Features extends Module {
  readonly 0 = conv2d({ in: 3, out: 16, k: 3 });
  readonly 3 = conv2d({ in: 16, out: 16, k: 3 });
  readonly 7 = conv2d({ in: 16, out: 32, k: 3 });
  readonly 10 = conv2d({ in: 32, out: 32, k: 3 });
  readonly 14 = conv2d({ in: 32, out: 48, k: 3 });
  readonly 17 = conv2d({ in: 48, out: 48, k: 3 });
  readonly 21 = conv2d({ in: 48, out: 64, k: 3 });
  readonly 24 = conv2d({ in: 64, out: 64, k: 3 });

  forward(x: Value): Value {
    const stages: [Conv, Conv][] = [
      [this[0], this[3]],
      [this[7], this[10]],
      [this[14], this[17]],
      [this[21], this[24]],
    ];
    let y = toHwc4(x);
    stages.forEach(([a, b], i) => {
      y = relu(b.forward(relu(a.forward(y))));
      const last = i === stages.length - 1;
      if (!last) y = maxPool2d(y, { kernelSize: 2, stride: 2, padding: 0 });
    });
    return y;
  }
}

class Classifier extends Module {
  readonly 1 = new Linear(64, 64);
  readonly 5 = new Linear(64, 32);
  readonly 9 = new Linear(32, 3);

  forward(x: Value): Value {
    let y = relu(this[1].forward(x));
    y = relu(this[5].forward(y));
    return this[9].forward(y);
  }
}

export class ColorBarModel extends Module {
  readonly features = new Features();
  readonly classifier = new Classifier();

  constructor() {
    super();
    // The conv stack runs hwc4+f16 (f16 biases); the MLP head stays f32.
    this.features.half();
  }

  forward(x: Value): Value {
    const pooled = toRow(globalAvgPool(this.features.forward(x)));
    return this.classifier.forward(pooled);
  }
}
