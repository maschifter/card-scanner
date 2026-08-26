import { Platform } from 'react-native';
import type { Frame, Point } from 'react-native-vision-camera';
import type { CameraRef } from 'react-native-vision-camera';
import type { BoundingBox } from '@cardnexus/card-scanner';

const IS_IOS = Platform.OS === 'ios';

/**
 * A detection box expressed as two opposite corners in camera sensor
 * coordinates - shared coordinate system that every VisionCamera v5 output gets
 */
export type CameraSpaceBox = { p1: Point; p2: Point };
export type ViewBox = { x: number; y: number; width: number; height: number };

/** Frame->camera snapshot, taken while the frame is alive and passed back with
 *  the scan result. iOS: [] (dims suffice); Android: affine [ox,oy,ax,ay,bx,by]. */
export function frameToCameraSnapshot(frame: Frame): number[] {
  'worklet';

  if (IS_IOS) {
    return [];
  }

  const w = frame.width;
  const h = frame.height;
  const o = frame.convertFramePointToCameraPoint({ x: 0, y: 0 });
  const px = frame.convertFramePointToCameraPoint({ x: w, y: 0 });
  const py = frame.convertFramePointToCameraPoint({ x: 0, y: h });
  return [
    o.x,
    o.y,
    (px.x - o.x) / w,
    (px.y - o.y) / w,
    (py.x - o.x) / h,
    (py.y - o.y) / h,
  ];
}

/** Maps a raw frame-buffer box (orientation handling lives in C++) into
 *  camera space via the coordinate snapshot from the result. JS thread. */
export function snapshotBoxToCameraSpace(
  box: BoundingBox,
  frameWidth: number,
  frameHeight: number,
  coordinateSnapshot: number[],
): CameraSpaceBox | null {
  if (IS_IOS) {
    // TODO:
    // Upstream 5.2.1 bug workaround (transposed boxes in portrait): the view
    // converter expects capture-device space, so just normalize buffer coords.
    return {
      p1: { x: box.x1 / frameWidth, y: box.y1 / frameHeight },
      p2: { x: box.x2 / frameWidth, y: box.y2 / frameHeight },
    };
  }

  if (coordinateSnapshot.length < 6) return null;
  const [ox, oy, ax, ay, bx, by] = coordinateSnapshot;
  return {
    p1: {
      x: ox + ax * box.x1 + bx * box.y1,
      y: oy + ay * box.x1 + by * box.y1,
    },
    p2: {
      x: ox + ax * box.x2 + bx * box.y2,
      y: oy + ay * box.x2 + by * box.y2,
    },
  };
}

/**
 * Maps a camera-space box to preview view coordinates, accounting for cropping, scaling, and rotation.
 * Rebuilds the rect from converted corners; returns null if the preview isn't ready yet.
 */
export function cameraSpaceBoxToViewBox(
  camera: CameraRef | null,
  box: CameraSpaceBox,
): ViewBox | null {
  if (camera == null) return null;
  try {
    const q1 = camera.convertCameraPointToViewPoint(box.p1);
    const q2 = camera.convertCameraPointToViewPoint(box.p2);
    return {
      x: Math.min(q1.x, q2.x),
      y: Math.min(q1.y, q2.y),
      width: Math.abs(q2.x - q1.x),
      height: Math.abs(q2.y - q1.y),
    };
  } catch {
    return null;
  }
}
