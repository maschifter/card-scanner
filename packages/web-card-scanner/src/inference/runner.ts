/** The InferenceSession of the web pipeline: one persistent input buffer,
 *  the forward captured once and replayed per call. A ruNNtime executor becomes
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
} from '../runntime.ts';
import type { TgpuRoot } from 'typegpu';
import { FIREFOX_POLL_MS } from '../constants.ts';
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

/** Firefox settles a readback only when it polls the device: on its 100 ms
 *  timer or on a submit (bug 1870699), so empty submits make it poll now. */
function settled<T>(device: GPUDevice, readback: Promise<T>): Promise<T> {
  if (!navigator.userAgent.includes('Firefox')) return readback;
  const poll = setInterval(() => device.queue.submit([]), FIREFOX_POLL_MS);
  return readback.finally(() => clearInterval(poll));
}

export class Runner {
  private readonly exec: ReturnType<typeof gpuExecutor>;
  private readonly input: Value;
  private readonly inputBuffer: GPUBuffer;
  /** Null for a runner fed prepared floats through runInput(). */
  private readonly frameInput: GpuFrameInput | null;
  private captured?: {
    replay: () => void;
    targets: Value[];
    profileReplay: () => Promise<KernelTiming[] | undefined>;
  };
  private staging?: GPUBuffer;

  /** The input height; the square size for a frame-fed model. */
  get size(): number {
    return this.dims[1];
  }

  constructor(
    private readonly root: TgpuRoot,
    private readonly dims: [number, number, number],
    mode: PreprocessMode | null,
    private readonly build: (input: Value) => Value[],
  ) {
    this.exec = gpuExecutor(root);
    const buf = uploadF32(root, new Float32Array(dims[0] * dims[1] * dims[2]));
    this.input = materialized(tensor3d(...dims), buf);
    this.inputBuffer = root.unwrap(buf);
    this.frameInput =
      mode === null
        ? null
        : new GpuFrameInput(root, dims[2], dims[1], mode, this.inputBuffer);
  }

  private dispatch(image: ImageData | GPUTexture, placement?: Placement) {
    if (!this.frameInput) {
      throw new Error(
        'Runner: this model takes prepared input; call runInput()',
      );
    }
    this.frameInput.write(image, placement);
    return this.replay();
  }

  private replay() {
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
    return this.read(this.dispatch(image, placement));
  }

  /** Runs on floats already laid out as the model's [C,H,W] input. */
  async runInput(data: Float32Array<ArrayBuffer>): Promise<Output[]> {
    this.root.device.queue.writeBuffer(this.inputBuffer, 0, data);
    return this.read(this.replay());
  }

  private async read(targets: Value[]): Promise<Output[]> {
    const data = await settled(
      this.root.device,
      this.exec.readbackMany!(
        targets.map((t) => ({ buffer: t.buffer, shape: t.shape })),
      ),
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

    await settled(device, staging.mapAsync(GPUMapMode.READ, 0, total));
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
    if (this.captured) return;
    if (this.frameInput) {
      await this.run(new ImageData(this.size, this.size));
    } else {
      const [c, h, w] = this.dims;
      await this.runInput(new Float32Array(c * h * w));
    }
  }

  async profile(): Promise<KernelTiming[] | undefined> {
    return this.captured?.profileReplay();
  }
}
