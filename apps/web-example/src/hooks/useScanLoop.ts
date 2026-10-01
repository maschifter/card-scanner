/** Display is not coupled to scanning: the Viewport paints the video at
 *  display rate and overlays whatever `cards` were last produced. */

import type { Dispatch, SetStateAction } from 'react';
import { useCallback, useEffect, useRef, useState } from 'react';
import {
  type ScannedCard,
  type ScanResult,
  type WebCardScanner,
} from '@cardnexus/web-card-scanner';
import { errorMessage } from '../scanner.ts';
import { useCamera } from './useCamera.ts';

export type { CameraSource } from './useCamera.ts';

/** What the UI renders per card. Deliberately NOT the ScannedCard: React's
 *  dev build deep-walks changed props to log what re-rendered, and a
 *  ScannedCard carries the dewarped ImageData (a million-entry pixel array)
 *  — walking it cost ~800 ms per scan and stalled the whole page. The
 *  thumbnail travels as an ImageBitmap, which is opaque to that walk. */
export type CardView = Omit<ScannedCard, 'dewarp' | 'embedding'> & {
  thumb: ImageBitmap;
};

function releaseThumbs(views: readonly CardView[]): CardView[] {
  views.forEach((v) => v.thumb.close());
  return [];
}

async function toViews(cards: ScannedCard[]): Promise<CardView[]> {
  return Promise.all(
    cards.map(async ({ dewarp, embedding: _embedding, ...rest }) => ({
      ...rest,
      thumb: await createImageBitmap(dewarp),
    })),
  );
}

export function useScanLoop(
  scanner: WebCardScanner | null,
  onError: Dispatch<SetStateAction<string>>,
) {
  const camera = useCamera(onError);
  const { sourceRef, startWebcam: openCamera, showImage, stop } = camera;
  const [cards, setCards] = useState<CardView[]>([]);
  const [timings, setTimings] = useState<ScanResult['timings'] | null>(null);
  const [rate, setRate] = useState(0);
  const [looping, setLooping] = useState(false);
  /** Longest main-thread stall (ms) seen since the previous scan — anything
   *  above ~50 ms is visible as a hitch in the camera feed. */
  const [stall, setStall] = useState(0);
  const stallRef = useRef(0);
  const busyRef = useRef(false);
  const doneAtRef = useRef<number[]>([]);

  useEffect(() => {
    if (typeof PerformanceObserver === 'undefined') return;
    const obs = new PerformanceObserver((list) => {
      for (const e of list.getEntries()) {
        stallRef.current = Math.max(stallRef.current, e.duration);
      }
    });
    try {
      obs.observe({ entryTypes: ['longtask'] });
    } catch {
      return; // Safari has no longtask entries
    }
    return () => obs.disconnect();
  }, []);

  const scanOnce = useCallback(async () => {
    const src = sourceRef.current;
    if (!src || !scanner || busyRef.current) return;
    busyRef.current = true;
    try {
      const result = await scanner.scan(src);
      const views = await toViews(result.cards);
      setCards((prev) => (releaseThumbs(prev), views));
      setTimings(result.timings);
      setStall(stallRef.current);
      stallRef.current = 0;
      const t = doneAtRef.current;
      t.push(performance.now());
      while (t.length > 10) t.shift();
      setRate(
        t.length > 1 ? ((t.length - 1) * 1000) / (t[t.length - 1] - t[0]) : 0,
      );
    } catch (e) {
      onError(`scan error: ${errorMessage(e)}`);
    } finally {
      busyRef.current = false;
    }
  }, [scanner, sourceRef, onError]);

  // The scan itself is the pacer; the setTimeout yield keeps input events
  // and the rAF painter responsive between scans.
  useEffect(() => {
    if (!looping) return;
    let active = true;
    (async () => {
      while (active) {
        await scanOnce();
        await new Promise((r) => setTimeout(r, 0));
      }
      // As on mobile; a still image keeps its result.
      if (sourceRef.current instanceof HTMLVideoElement)
        setCards(releaseThumbs);
    })();
    return () => {
      active = false;
    };
  }, [looping, scanOnce, sourceRef]);

  const stopWebcam = useCallback(() => {
    stop();
    doneAtRef.current = [];
    setLooping(false);
    setCards(releaseThumbs);
    setRate(0);
  }, [stop]);

  const startWebcam = useCallback(async () => {
    if (await openCamera()) setLooping(true);
  }, [openCamera]);

  const loadImage = useCallback(
    async (url: string) => {
      stopWebcam();
      await showImage(url);
      await scanOnce();
    },
    [stopWebcam, showImage, scanOnce],
  );

  return {
    source: camera.source,
    webcam: camera.source instanceof HTMLVideoElement,
    cards,
    timings,
    rate,
    stall,
    looping,
    toggleLoop: () => setLooping((v) => !v),
    scanOnce,
    startWebcam,
    stopWebcam,
    loadImage,
  };
}
