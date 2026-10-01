/** Runs each TIE port on the fixture input apps/web-example/tools/export-web-models.py
 *  captured and compares against the ONNX output.
 *
 *  The YOLO fixtures cover the prototype-mask head only: the deployed ONNX
 *  exports the one2many training towers (app-side NMS), while TIE runs the
 *  end-to-end one2one towers, so the concatenated box output has no
 *  tensor-level counterpart. The proto head shares the whole backbone+neck,
 *  which is the risky part. SetSymbolDetectionModel has no proto, so it is
 *  covered behaviorally by the app (and structurally by CardSegmentation
 *  sharing the same zoo code). */

import tgpu from 'typegpu';
import {
  createOpfsCache,
  fromSafetensors,
  initTie,
  tensor,
  tensor3d,
  toArray,
  type Value,
} from '@tie/core';
import { loadYolo26Weights, Yolo26Model } from '@tie/zoo/yolo26';
import {
  CardRecognitionModel,
  ColorBarModel,
  l2Normalize,
  SetSymbolRecognitionModel,
} from '@cardnexus/web-card-scanner';
import { reportPageErrors } from '../lib/pageErrors.ts';

const logEl = document.getElementById('log')!;
reportPageErrors((message) => {
  logEl.textContent += `\n${message}`;
});

const table = document.getElementById('results') as HTMLTableElement;
table.innerHTML =
  '<tr><th>model</th><th>output</th><th>max abs diff</th><th>verdict</th></tr>';

const row = (
  model: string,
  output: string,
  diff: number,
  tolerance: number,
) => {
  const tr = table.insertRow();
  const ok = diff <= tolerance;
  tr.innerHTML =
    `<td>${model}</td><td>${output}</td><td>${diff.toExponential(2)}</td>` +
    `<td class="${ok ? 'pass' : 'fail'}">${ok ? 'PASS' : `FAIL (tol ${tolerance})`}</td>`;
  return ok;
};

async function fetchF32(url: string): Promise<Float32Array> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: ${res.status}`);
  return new Float32Array(await res.arrayBuffer());
}

function maxAbsDiff(a: Float32Array, b: Float32Array): number {
  let m = 0;
  for (let i = 0; i < a.length; i++) m = Math.max(m, Math.abs(a[i] - b[i]));
  return m;
}

async function main() {
  const root = await tgpu.init({
    device: {
      optionalFeatures: ['shader-f16', 'timestamp-query', 'subgroups'],
    },
  });
  initTie(root);
  const cache = await createOpfsCache('web-card-scanner').catch(
    () => undefined,
  );
  const results: boolean[] = [];

  const run = async (
    name: string,
    dims: [number, number, number],
    forward: (x: Value) => Value,
    transform: (out: Float32Array) => Float32Array = (o) => o,
    outputName = 'embedding',
    tolerance = 2e-2,
  ) => {
    logEl.textContent = `running ${name}…`;
    const input = await fetchF32(`/models/${name}/fixture/input.bin`);
    const expected = await fetchF32(
      `/models/${name}/fixture/output.${outputName}.bin`,
    );
    const x = tensor(input, tensor3d(...dims));
    const got = transform(await toArray(forward(x)));
    results.push(row(name, outputName, maxAbsDiff(got, expected), tolerance));
  };

  {
    const model = new CardRecognitionModel();
    const url = '/models/CardRecognitionModel/model.safetensors';
    await model.loadStateDict(
      await fromSafetensors(url, { cache, cacheId: url }),
      { root },
    );
    await run(
      'CardRecognitionModel',
      [3, 224, 224],
      (x) => model.forward(x),
      l2Normalize,
    );
  }
  {
    const model = new SetSymbolRecognitionModel();
    const url = '/models/SetSymbolRecognitionModel/model.safetensors';
    await model.loadStateDict(
      await fromSafetensors(url, { cache, cacheId: url }),
      { root },
    );
    await run(
      'SetSymbolRecognitionModel',
      [3, 96, 96],
      (x) => model.forward(x),
      l2Normalize,
    );
  }
  {
    const model = new ColorBarModel();
    const url = '/models/ColorBarModel/model.safetensors';
    await model.loadStateDict(
      await fromSafetensors(url, { cache, cacheId: url }),
      { root },
    );
    await run(
      'ColorBarModel',
      [3, 100, 100],
      (x) => model.forward(x),
      (o) => o,
      'logits',
    );
  }
  {
    const model = new Yolo26Model('n', { numClasses: 17, task: 'segment' });
    await loadYolo26Weights(model, '/models/CardSegmentationModel', { cache });
    await run(
      'CardSegmentationModel',
      [3, 384, 384],
      (x) => model.forward(x).proto,
      (o) => o,
      'output1',
      5e-2,
    );
  }

  logEl.textContent = results.every(Boolean)
    ? `all ${results.length} checks passed`
    : 'FAILURES — see table';
}

main().catch((e) => {
  logEl.textContent = `error: ${e instanceof Error ? e.message : String(e)}`;
  throw e;
});
