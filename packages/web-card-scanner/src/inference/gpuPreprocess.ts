/** Pixels → normalized CHW f32 in a TIE input buffer, sampled in one compute
 *  dispatch through a placement (crop, squash, or letterbox). The sampling
 *  matches cv::resize INTER_LINEAR more closely than canvas drawImage does.
 *
 *  rgba8unorm texture samples already yield [0,1] floats, so 'scale255' is
 *  a plain copy. */

import type { TgpuRoot } from 'typegpu';
import { IMAGENET_MEAN, IMAGENET_STD, LETTERBOX_PAD } from '../constants.ts';

export type PreprocessMode = 'scale255' | 'imagenet';

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

/** `src` (source pixels) lands on `dst` (output pixels); output pixels
 *  outside `dst` take the letterbox pad colour. */
export interface Placement {
  src: Rect;
  dst: Rect;
}

export function cropPlacement(src: Rect, size: number): Placement {
  return { src, dst: { x: 0, y: 0, w: size, h: size } };
}

export interface Letterbox {
  placement: Placement;
  /** Source pixel → model-input pixel. */
  scale: number;
  /** Model-input pixel → source pixel. */
  toSource(x: number, y: number): { x: number; y: number };
}

export function letterboxPlacement(
  srcW: number,
  srcH: number,
  size: number,
): Letterbox {
  const scale = Math.min(size / srcW, size / srcH);
  const w = Math.round(srcW * scale);
  const h = Math.round(srcH * scale);
  // Whole-pixel pads, as native and ultralytics round(d - 0.1).
  const padX = Math.floor((size - w) / 2);
  const padY = Math.floor((size - h) / 2);
  return {
    placement: {
      src: { x: 0, y: 0, w: srcW, h: srcH },
      dst: { x: padX, y: padY, w, h },
    },
    scale,
    toSource: (x, y) => ({ x: (x - padX) / scale, y: (y - padY) / scale }),
  };
}

/** Pipelines are per (device, size, mode) — size and mode are baked into the
 *  shader source so the loop bounds constant-fold. */
const pipelineCache = new WeakMap<GPUDevice, Map<string, GPUComputePipeline>>();

function pipelineFor(
  device: GPUDevice,
  size: number,
  mode: PreprocessMode,
): GPUComputePipeline {
  let bySpec = pipelineCache.get(device);
  if (!bySpec) {
    bySpec = new Map();
    pipelineCache.set(device, bySpec);
  }
  const key = `${size}:${mode}`;
  let pipeline = bySpec.get(key);
  if (pipeline) return pipeline;
  const hw = size * size;
  const normalize =
    mode === 'imagenet'
      ? `v = (v - vec3f(${IMAGENET_MEAN.join(', ')})) / vec3f(${IMAGENET_STD.join(', ')});`
      : ``;
  const module = device.createShaderModule({
    code: /* wgsl */ `
      struct Map {
        src: vec4f, // x, y, w, h in source pixels
        dst: vec4f, // x, y, w, h in output pixels
        tex: vec2f, // source texture size
        pad: vec2f,
      };
      @group(0) @binding(0) var src: texture_2d<f32>;
      @group(0) @binding(1) var samp: sampler;
      @group(0) @binding(2) var<storage, read_write> dst: array<f32>;
      @group(0) @binding(3) var<uniform> map: Map;
      @compute @workgroup_size(64)
      fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
        let i = gid.x;
        if (i >= ${hw}u) { return; }
        let px = f32(i % ${size}u) + 0.5;
        let py = f32(i / ${size}u) + 0.5;
        var v = vec3f(${LETTERBOX_PAD / 255});
        let inside = px >= map.dst.x && px < map.dst.x + map.dst.z
          && py >= map.dst.y && py < map.dst.y + map.dst.w;
        if (inside) {
          let u = (px - map.dst.x) / map.dst.z;
          let w = (py - map.dst.y) / map.dst.w;
          let s = map.src.xy + vec2f(u, w) * map.src.zw;
          v = textureSampleLevel(src, samp, s / map.tex, 0.0).rgb;
        }
        ${normalize}
        dst[i] = v.r;
        dst[i + ${hw}u] = v.g;
        dst[i + ${2 * hw}u] = v.b;
      }`,
  });
  pipeline = device.createComputePipeline({
    layout: 'auto',
    compute: { module, entryPoint: 'main' },
  });
  bySpec.set(key, pipeline);
  return pipeline;
}

/** The `Map` uniform: src rect, dst rect, texture size, padding — 12 f32. */
function packMap(p: Placement, texW: number, texH: number) {
  const { src, dst } = p;
  const map = new Float32Array(12);
  map.set([src.x, src.y, src.w, src.h], 0);
  map.set([dst.x, dst.y, dst.w, dst.h], 4);
  map.set([texW, texH], 8);
  return map;
}

export class GpuFrameInput {
  private readonly device: GPUDevice;
  private readonly pipeline: GPUComputePipeline;
  private readonly sampler: GPUSampler;
  private readonly map: GPUBuffer;
  private tex?: GPUTexture;
  private bind?: { texture: GPUTexture; group: GPUBindGroup };

  constructor(
    root: TgpuRoot,
    private readonly size: number,
    mode: PreprocessMode,
    private readonly dst: GPUBuffer,
  ) {
    this.device = root.device;
    this.pipeline = pipelineFor(this.device, size, mode);
    this.sampler = this.device.createSampler({
      magFilter: 'linear',
      minFilter: 'linear',
      addressModeU: 'clamp-to-edge',
      addressModeV: 'clamp-to-edge',
    });
    this.map = this.device.createBuffer({
      size: 48,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
  }

  private uploadTexture(w: number, h: number): GPUTexture {
    if (this.tex?.width === w && this.tex.height === h) return this.tex;
    this.tex?.destroy();
    this.tex = this.device.createTexture({
      size: [w, h],
      format: 'rgba8unorm',
      usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING,
    });
    return this.tex;
  }

  private bindingsFor(texture: GPUTexture): GPUBindGroup {
    if (this.bind?.texture !== texture) {
      this.bind = {
        texture,
        group: this.device.createBindGroup({
          layout: this.pipeline.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: texture.createView() },
            { binding: 1, resource: this.sampler },
            { binding: 2, resource: { buffer: this.dst } },
            { binding: 3, resource: { buffer: this.map } },
          ],
        }),
      };
    }
    return this.bind.group;
  }

  /** Submits its own pass; queue order keeps it ahead of the model's replay. */
  write(image: ImageData | GPUTexture, placement?: Placement): void {
    const { width, height } = image;
    const whole = { x: 0, y: 0, w: width, h: height };
    const p = placement ?? cropPlacement(whole, this.size);

    const queue = this.device.queue;
    let texture: GPUTexture;
    if (image instanceof ImageData) {
      texture = this.uploadTexture(width, height);
      queue.writeTexture({ texture }, image.data, { bytesPerRow: width * 4 }, [
        width,
        height,
      ]);
    } else {
      texture = image;
    }
    queue.writeBuffer(this.map, 0, packMap(p, width, height));

    const encoder = this.device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    pass.setPipeline(this.pipeline);
    pass.setBindGroup(0, this.bindingsFor(texture));
    pass.dispatchWorkgroups(Math.ceil((this.size * this.size) / 64));
    pass.end();
    queue.submit([encoder.finish()]);
  }
}
