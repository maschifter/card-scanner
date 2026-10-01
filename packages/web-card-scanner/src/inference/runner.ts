/** The InferenceSession of the web pipeline: one persistent input buffer,
 *  the forward captured once and replayed per call. A TIE executor becomes
 *  replay-only after captureFrame, so every runner owns a private one. */

import {
  evalValues,
  gpuExecutor,
  materialized,
  tensor3d,
  uploadF32,
  type F32Buffer,
  type KernelTiming,
  type Value,
} from '@tie/core';
import type { TgpuRoot } from 'typegpu';
import {
  GpuFrameInput,
  type Placement,
  type PreprocessMode,
} from './gpuPreprocess.ts';
import { paddedRowBytes, unpadRows } from './gpuFrame.ts';

/** One model output with its shape (batch dimension stripped). */
export interface Output {
  data: Float32Array;
  dims: readonly number[];
}

export class Runner {
  private readonly exec: ReturnType<typeof gpuExecutor>;
  private readonly input: Value;
  private readonly frameInput: GpuFrameInput;
  private captured?: {
    replay: () => void;
    targets: Value[];
    profileReplay: () => Promise<KernelTiming[] | undefined>;
  };
  readonly size: number;
  private staging?: GPUBuffer;

  constructor(
    private readonly root: TgpuRoot,
    dims: [number, number, number],
    mode: PreprocessMode,
    private readonly build: (input: Value) => Value[],
  ) {
    this.exec = gpuExecutor(root);
    this.size = dims[1];
    const buf = uploadF32(root, new Float32Array(dims[0] * dims[1] * dims[2]));
    this.input = materialized(tensor3d(...dims), buf);
    this.frameInput = new GpuFrameInput(root, dims[1], mode, root.unwrap(buf));
  }

  private dispatch(image: ImageData | GPUTexture, placement?: Placement) {
    this.frameInput.write(image, placement);
    if (this.captured) {
      this.captured.replay();
    } else {
      this.captured = this.exec.captureFrame(() => {
        const targets = this.build(this.input);
        evalValues(targets, this.exec);
        return targets;
      });
    }
    return this.captured.targets;
  }

  async run(
    image: ImageData | GPUTexture,
    placement?: Placement,
  ): Promise<Output[]> {
    const targets = this.dispatch(image, placement);
    const data = await this.exec.readbackMany!(
      targets.map((t) => ({ buffer: t.buffer, shape: t.shape })),
    );
    return data.map((d, i) => ({
      data: d,
      dims: targets[i].shape.dims ?? [],
    }));
  }

  /** run() plus the pixels in one mapAsync; f32 outputs, one texture size. */
  async runAndRead(
    texture: GPUTexture,
  ): Promise<{ outputs: Output[]; pixels: ImageData }> {
    const targets = this.dispatch(texture);
    const spans = targets.map((t) => ({
      buffer: this.root.unwrap(t.buffer as F32Buffer),
      bytes: t.shape.elems * 4,
    }));
    const outBytes = spans.reduce((s, x) => s + x.bytes, 0);
    const rowBytes = paddedRowBytes(texture.width);
    const total = outBytes + rowBytes * texture.height;

    const device = this.root.device;
    const staging = (this.staging ??= device.createBuffer({
      size: total,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    }));
    const encoder = device.createCommandEncoder();
    let offset = 0;
    for (const s of spans) {
      encoder.copyBufferToBuffer(s.buffer, 0, staging, offset, s.bytes);
      offset += s.bytes;
    }
    encoder.copyTextureToBuffer(
      { texture },
      {
        buffer: staging,
        offset: outBytes,
        bytesPerRow: rowBytes,
        rowsPerImage: texture.height,
      },
      [texture.width, texture.height],
    );
    device.queue.submit([encoder.finish()]);

    await staging.mapAsync(GPUMapMode.READ, 0, total);
    const mapped = staging.getMappedRange(0, total);
    let at = 0;
    const outputs = spans.map((s, i) => {
      const data = new Float32Array(mapped.slice(at, at + s.bytes));
      at += s.bytes;
      return { data, dims: targets[i].shape.dims ?? [] };
    });
    const pixels = unpadRows(mapped, outBytes, texture.width, texture.height);
    staging.unmap();
    return { outputs, pixels };
  }

  /** Shader compilation is the one expensive step (the HWC4 kernels compile
   *  one module per layer geometry, ~100 per YOLO model) and runs on the main
   *  thread; at load it stays out of the camera loop, where a model that
   *  first runs on the first detection would stall the page. */
  async warmUp(): Promise<void> {
    if (!this.captured) await this.run(new ImageData(this.size, this.size));
  }

  async profile(): Promise<KernelTiming[] | undefined> {
    return this.captured?.profileReplay();
  }
}
