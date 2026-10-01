/** Keeps the frame on the GPU and dewarps the card there. */

import { DEWARP_HEIGHT, DEWARP_WIDTH } from '../constants.ts';
import type { FrameSource } from '../types.ts';
import { rectToQuadHomography, type Quad } from '../utils/geometry.ts';
import { captureFrame, captureSize, sourceSize } from '../utils/frame.ts';

const FORMAT: GPUTextureFormat = 'rgba8unorm';
const WORKGROUP = 8;

// Bilinear at pixel centres: like drawImage up to 2× down, exact at scale 1.
const RESAMPLE = /* wgsl */ `
  @group(0) @binding(0) var src: texture_2d<f32>;
  @group(0) @binding(1) var samp: sampler;
  @group(0) @binding(2) var dst: texture_storage_2d<rgba8unorm, write>;
  @compute @workgroup_size(${WORKGROUP}, ${WORKGROUP})
  fn main(@builtin(global_invocation_id) gid: vec3u) {
    let size = textureDimensions(dst);
    if (gid.x >= size.x || gid.y >= size.y) { return; }
    let uv = (vec2f(gid.xy) + 0.5) / vec2f(size);
    let c = textureSampleLevel(src, samp, uv, 0.0);
    textureStore(dst, vec2i(gid.xy), vec4f(c.rgb, 1.0));
  }`;

const DEWARP = /* wgsl */ `
  struct Warp { r0: vec4f, r1: vec4f, r2: vec4f, size: vec4f };
  @group(0) @binding(0) var src: texture_2d<f32>;
  @group(0) @binding(1) var dst: texture_storage_2d<rgba8unorm, write>;
  @group(0) @binding(2) var<uniform> warp: Warp;
  @compute @workgroup_size(${WORKGROUP}, ${WORKGROUP})
  fn main(@builtin(global_invocation_id) gid: vec3u) {
    let size = textureDimensions(dst);
    if (gid.x >= size.x || gid.y >= size.y) { return; }
    let p = vec3f(f32(gid.x), f32(gid.y), 1.0);
    let n = vec3f(dot(warp.r0.xyz, p), dot(warp.r1.xyz, p), dot(warp.r2.xyz, p));
    let px = n.x / n.z;
    let py = n.y / n.z;
    let maxX = warp.size.x - 1.0;
    let maxY = warp.size.y - 1.0;
    var rgb = vec3f(0.0);
    if (px >= 0.0 && py >= 0.0 && px <= maxX && py <= maxY) {
      let x0 = floor(px);
      let y0 = floor(py);
      let x1 = min(x0 + 1.0, maxX);
      let y1 = min(y0 + 1.0, maxY);
      let fx = px - x0;
      let fy = py - y0;
      let c00 = textureLoad(src, vec2i(i32(x0), i32(y0)), 0).rgb;
      let c10 = textureLoad(src, vec2i(i32(x1), i32(y0)), 0).rgb;
      let c01 = textureLoad(src, vec2i(i32(x0), i32(y1)), 0).rgb;
      let c11 = textureLoad(src, vec2i(i32(x1), i32(y1)), 0).rgb;
      rgb = c00 * ((1.0 - fx) * (1.0 - fy)) + c10 * (fx * (1.0 - fy))
          + c01 * ((1.0 - fx) * fy) + c11 * (fx * fy);
    }
    textureStore(dst, vec2i(gid.xy), vec4f(rgb, 1.0));
  }`;

const computePipeline = (device: GPUDevice, code: string) =>
  device.createComputePipeline({
    layout: 'auto',
    compute: {
      module: device.createShaderModule({ code }),
      entryPoint: 'main',
    },
  });

const dispatchGrid = (
  device: GPUDevice,
  pipeline: GPUComputePipeline,
  bind: GPUBindGroup,
  w: number,
  h: number,
) => {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bind);
  pass.dispatchWorkgroups(Math.ceil(w / WORKGROUP), Math.ceil(h / WORKGROUP));
  pass.end();
  device.queue.submit([encoder.finish()]);
};

export class GpuFrame {
  private readonly kernel: GPUComputePipeline;
  private readonly sampler: GPUSampler;
  private frame?: GPUTexture;
  /** Full-resolution landing texture for video frames. */
  private raw?: GPUTexture;
  private resampleBind?: GPUBindGroup;
  /** False once the browser rejects a video (Firefox); the canvas copies it. */
  private videoCopy = true;

  constructor(private readonly device: GPUDevice) {
    this.kernel = computePipeline(device, RESAMPLE);
    this.sampler = device.createSampler({
      magFilter: 'linear',
      minFilter: 'linear',
      addressModeU: 'clamp-to-edge',
      addressModeV: 'clamp-to-edge',
    });
  }

  /** Images use the canvas: a 12 MP photo's direct GPU copy takes ~90 ms. */
  upload(source: FrameSource): GPUTexture | null {
    const size = sourceSize(source);
    if (!size) return null;
    const queue = this.device.queue;

    if (!(source instanceof HTMLVideoElement) || !this.videoCopy) {
      const pixels =
        source instanceof ImageData
          ? source
          : captureFrame(source, size.width, size.height);
      const frame = this.frameTexture(pixels.width, pixels.height);
      queue.writeTexture(
        { texture: frame },
        pixels.data,
        { bytesPerRow: pixels.width * 4 },
        [pixels.width, pixels.height],
      );
      return frame;
    }

    const { width, height } = captureSize(size.width, size.height);
    const frame = this.frameTexture(width, height);
    const raw = this.rawTexture(size.width, size.height);
    try {
      queue.copyExternalImageToTexture({ source }, { texture: raw }, [
        size.width,
        size.height,
      ]);
    } catch {
      this.videoCopy = false;
      return this.upload(source);
    }
    this.resampleBind ??= this.device.createBindGroup({
      layout: this.kernel.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: raw.createView() },
        { binding: 1, resource: this.sampler },
        { binding: 2, resource: frame.createView() },
      ],
    });
    dispatchGrid(this.device, this.kernel, this.resampleBind, width, height);
    return frame;
  }

  private frameTexture(w: number, h: number): GPUTexture {
    if (this.frame?.width === w && this.frame.height === h) return this.frame;
    this.frame?.destroy();
    this.resampleBind = undefined;
    this.frame = this.device.createTexture({
      size: [w, h],
      format: FORMAT,
      usage:
        GPUTextureUsage.TEXTURE_BINDING |
        GPUTextureUsage.STORAGE_BINDING |
        GPUTextureUsage.COPY_DST,
    });
    return this.frame;
  }

  private rawTexture(w: number, h: number): GPUTexture {
    if (this.raw?.width === w && this.raw.height === h) return this.raw;
    this.raw?.destroy();
    this.resampleBind = undefined;
    // copyExternalImageToTexture needs RENDER_ATTACHMENT on the destination.
    this.raw = this.device.createTexture({
      size: [w, h],
      format: FORMAT,
      usage:
        GPUTextureUsage.TEXTURE_BINDING |
        GPUTextureUsage.COPY_DST |
        GPUTextureUsage.RENDER_ATTACHMENT,
    });
    return this.raw;
  }
}

export class GpuDewarp {
  readonly texture: GPUTexture;
  private readonly kernel: GPUComputePipeline;
  private readonly uniform: GPUBuffer;
  private bind?: { frame: GPUTexture; group: GPUBindGroup };

  constructor(private readonly device: GPUDevice) {
    this.kernel = computePipeline(device, DEWARP);
    this.texture = device.createTexture({
      size: [DEWARP_WIDTH, DEWARP_HEIGHT],
      format: FORMAT,
      usage:
        GPUTextureUsage.TEXTURE_BINDING |
        GPUTextureUsage.STORAGE_BINDING |
        GPUTextureUsage.COPY_SRC,
    });
    this.uniform = device.createBuffer({
      size: 64,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
  }

  /** Warps `quad` (TL, TR, BR, BL in frame pixels) of `frame` upright. */
  run(frame: GPUTexture, quad: Quad): void {
    const h = rectToQuadHomography(quad, DEWARP_WIDTH, DEWARP_HEIGHT);
    const u = new Float32Array(16);
    u.set([h[0], h[1], h[2], 0, h[3], h[4], h[5], 0, h[6], h[7], h[8], 0]);
    u.set([frame.width, frame.height], 12);
    this.device.queue.writeBuffer(this.uniform, 0, u);
    if (this.bind?.frame !== frame) {
      this.bind = {
        frame,
        group: this.device.createBindGroup({
          layout: this.kernel.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: frame.createView() },
            { binding: 1, resource: this.texture.createView() },
            { binding: 2, resource: { buffer: this.uniform } },
          ],
        }),
      };
    }
    dispatchGrid(
      this.device,
      this.kernel,
      this.bind.group,
      DEWARP_WIDTH,
      DEWARP_HEIGHT,
    );
  }
}

/** Warping to the quad with its corners shifted by two turns the card 180°. */
export function rotateQuad180(q: Quad): Quad {
  return [q[2], q[3], q[0], q[1]];
}

/** Bytes per texture row in a texture-to-buffer copy (WebGPU requires 256). */
export const paddedRowBytes = (width: number) =>
  Math.ceil((width * 4) / 256) * 256;

export function unpadRows(
  bytes: ArrayBuffer,
  offset: number,
  width: number,
  height: number,
): ImageData {
  const out = new ImageData(width, height);
  const row = paddedRowBytes(width);
  for (let y = 0; y < height; y++) {
    out.data.set(
      new Uint8Array(bytes, offset + y * row, width * 4),
      y * width * 4,
    );
  }
  return out;
}
