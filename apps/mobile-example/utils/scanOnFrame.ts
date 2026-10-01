import type { AsyncRunner, Frame } from 'react-native-vision-camera';
import type { Synchronizable } from 'react-native-worklets';
import { cardScannerPlugin } from '@cardnexus/card-scanner';
import { frameToCameraSnapshot } from './cameraCoords';

/**
 * Builds the `onFrame` worklet shared by the scanner screens: snapshot the
 * coordinate mapping while the frame is alive, then scan on the runner thread.
 *
 * Frame ownership: `scanFrame` takes the Frame and disposes it itself (early,
 * right after copying the pixels). The worklet disposes only the frames that
 * never reach `scanFrame` - not scanning, or the runner was still busy.
 */
export function createScanOnFrame(
  asyncRunner: AsyncRunner,
  isScanningSync: Synchronizable<boolean>,
) {
  return (frame: Frame) => {
    'worklet';

    if (!isScanningSync.getDirty()) {
      frame.dispose();
      return;
    }

    let scheduled = false;
    try {
      const snapshot = frameToCameraSnapshot(frame);
      scheduled = asyncRunner.runAsync(() => {
        'worklet';
        try {
          cardScannerPlugin.scanFrame(frame, snapshot);
        } catch (e) {
          console.error('[CardScanner] scanFrame failed in scan task', e);
          if (frame.isValid) frame.dispose();
        }
      });
    } catch {
    } finally {
      if (!scheduled) {
        frame.dispose();
      }
    }
  };
}
