import { FRAME_MAX_WIDTH } from '../constants.ts';
import type { FrameSource } from '../types.ts';

/** Null until the source has dimensions (a video that has not started). */
export function sourceSize(
  source: FrameSource,
): { width: number; height: number } | null {
  const [width, height] =
    source instanceof HTMLVideoElement
      ? [source.videoWidth, source.videoHeight]
      : source instanceof HTMLImageElement
        ? [source.naturalWidth, source.naturalHeight]
        : [source.width, source.height];
  return width && height ? { width, height } : null;
}

export function captureSize(w: number, h: number) {
  const scale = Math.min(1, FRAME_MAX_WIDTH / w);
  return { width: Math.round(w * scale), height: Math.round(h * scale) };
}

// One shared scratch canvas: calls are sequential on the main thread.
let grab: HTMLCanvasElement | undefined;

export function captureFrame(
  source: HTMLVideoElement | HTMLImageElement,
  w: number,
  h: number,
): ImageData {
  const { width, height } = captureSize(w, h);
  grab ??= document.createElement('canvas');
  grab.width = width;
  grab.height = height;
  const ctx = grab.getContext('2d', { willReadFrequently: true })!;
  ctx.drawImage(source, 0, 0, width, height);
  return ctx.getImageData(0, 0, width, height);
}
