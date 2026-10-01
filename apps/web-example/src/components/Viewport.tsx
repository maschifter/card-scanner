/** Live overlays are smoothed the way the mobile BoundingBox animates its
 *  rect: each shown box eases toward the detection it overlaps, and a
 *  frame with no detections keeps the last box for BOX_CLEAR_GRACE_MS
 *  (VisionCameraScanner's grace), so a single missed frame does not blink
 *  the box off. */

import { useEffect, useRef } from 'react';
import { captureSize, iou, sourceSize } from '@cardnexus/web-card-scanner';
import type { CardView, CameraSource } from '../hooks/useScanLoop.ts';
import { drawCardsOverlay } from '../lib/overlay.ts';

const BOX_CLEAR_GRACE_MS = 400;
/** Per-frame easing toward the target box (1 = snap). */
const EASE = 0.35;
const OVERLAY_SAME_CARD_MIN_IOU = 0.3;

const lerp = (a: number, b: number) => a + (b - a) * EASE;

function ease(shown: CardView, target: CardView): CardView {
  return {
    ...target,
    box: {
      x1: lerp(shown.box.x1, target.box.x1),
      y1: lerp(shown.box.y1, target.box.y1),
      x2: lerp(shown.box.x2, target.box.x2),
      y2: lerp(shown.box.y2, target.box.y2),
    },
  };
}

function paint(
  canvas: HTMLCanvasElement,
  source: CameraSource,
  cards: readonly CardView[],
) {
  const size = sourceSize(source);
  if (!size) return;
  const { width, height } = captureSize(size.width, size.height);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const ctx = canvas.getContext('2d')!;
  ctx.drawImage(source, 0, 0, width, height);
  drawCardsOverlay(ctx, cards);
}

export function Viewport({
  source,
  cards,
}: {
  source: CameraSource | null;
  cards: CardView[];
}) {
  const ref = useRef<HTMLCanvasElement>(null);
  const targetRef = useRef<CardView[]>(cards);
  const lastSeenRef = useRef(0);
  const shownRef = useRef<CardView[]>([]);
  useEffect(() => {
    targetRef.current = cards;
    if (cards.length) lastSeenRef.current = performance.now();
  }, [cards]);

  useEffect(() => {
    if (!(source instanceof HTMLVideoElement)) return;
    shownRef.current = [];
    let raf = 0;
    const tick = () => {
      let target = targetRef.current;
      if (
        target.length === 0 &&
        performance.now() - lastSeenRef.current < BOX_CLEAR_GRACE_MS
      ) {
        target = shownRef.current; // grace: hold the last box
      }
      const prev = shownRef.current;
      shownRef.current = target.map((t) => {
        const match = prev.find(
          (s) => iou(s.box, t.box) >= OVERLAY_SAME_CARD_MIN_IOU,
        );
        return match ? ease(match, t) : t;
      });
      paint(ref.current!, source, shownRef.current);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [source]);

  useEffect(() => {
    if (source instanceof HTMLImageElement) paint(ref.current!, source, cards);
  }, [source, cards]);

  return <canvas ref={ref} className="viewport" width={640} height={480} />;
}
