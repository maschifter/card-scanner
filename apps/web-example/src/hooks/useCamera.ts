/** The current source is a ref for the scan loop and state for the Viewport. */

import type { Dispatch, SetStateAction } from 'react';
import { useCallback, useEffect, useRef, useState } from 'react';
import type { FrameSource } from '@cardnexus/web-card-scanner';
import { errorMessage } from '../scanner.ts';

export type CameraSource = Exclude<FrameSource, ImageData>;

export function useCamera(onError: Dispatch<SetStateAction<string>>) {
  const [source, setSource] = useState<CameraSource | null>(null);
  const sourceRef = useRef<CameraSource | null>(null);
  const streamRef = useRef<MediaStream | null>(null);

  const stop = useCallback(() => {
    streamRef.current?.getTracks().forEach((t) => t.stop());
    streamRef.current = null;
    sourceRef.current = null;
    setSource(null);
  }, []);

  const startWebcam = useCallback(async (): Promise<boolean> => {
    let stream: MediaStream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        video: {
          facingMode: 'environment',
          width: { ideal: 1920 },
          height: { ideal: 1080 },
        },
      });
    } catch (e) {
      onError(`webcam error: ${errorMessage(e)}`);
      return false;
    }
    onError((s) => (s.startsWith('webcam error') ? 'webcam on' : s));
    const video = document.createElement('video');
    video.srcObject = stream;
    video.muted = true;
    video.playsInline = true;
    await video.play();
    streamRef.current = stream;
    sourceRef.current = video;
    setSource(video);
    return true;
  }, [onError]);

  const showImage = useCallback(
    async (url: string) => {
      stop();
      const img = new Image();
      img.src = url;
      await img.decode();
      sourceRef.current = img;
      setSource(img);
    },
    [stop],
  );

  useEffect(() => stop, [stop]);

  return { source, sourceRef, startWebcam, showImage, stop };
}
