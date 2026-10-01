/** Sticky single-card selection — core/CardSelection.cpp selectCenterMost.
 *  Keeps following the card it picked (IoU match) until it is gone for
 *  TRACKING_LOST_AFTER_MS; a rival takes over only when clearly closer to the
 *  frame centre, or dead-centre while the tracked card is missed. That is
 *  what stops the selection bouncing between two cards frame to frame. */

import {
  DEAD_CENTER_RADIUS_FRAC,
  RIVAL_TAKEOVER_DIST_FRAC,
  SAME_CARD_MIN_IOU,
  TRACKING_LOST_AFTER_MS,
} from '../constants.ts';
import { argMin } from '../utils/array.ts';
import { type Box, iou } from '../utils/box.ts';
import type { Pt, Quad } from '../utils/geometry.ts';

export interface Trackable {
  box: Box;
  /** Frame-space card quad when the mask produced one. Its centroid tracks
   *  the card itself, so tilted and sideways cards stay stable. */
  quad: Quad | null;
}

function center(t: Trackable): Pt {
  if (t.quad) {
    return {
      x: t.quad.reduce((s, p) => s + p.x, 0) / 4,
      y: t.quad.reduce((s, p) => s + p.y, 0) / 4,
    };
  }
  return { x: (t.box.x1 + t.box.x2) / 2, y: (t.box.y1 + t.box.y2) / 2 };
}

export class StickyTracker {
  private tracked: { box: Box; lastSeen: number } | null = null;

  select<T extends Trackable>(
    items: readonly T[],
    frameW: number,
    frameH: number,
    now = performance.now(),
  ): T[] {
    if (items.length <= 1) {
      if (items[0]) this.remember(items[0].box, now);
      return [...items];
    }

    const cx = frameW / 2;
    const cy = frameH / 2;
    const distSq = items.map((it) => {
      const c = center(it);
      return (c.x - cx) ** 2 + (c.y - cy) ** 2;
    });
    const nearest = argMin([...distSq.keys()], (i) => distSq[i]);

    if (this.tracked && now - this.tracked.lastSeen > TRACKING_LOST_AFTER_MS) {
      this.tracked = null;
    }

    let winner = nearest;
    if (this.tracked) {
      const current = this.matchTracked(items);
      if (current >= 0) {
        // Keep the tracked card unless a rival is clearly closer to the
        // frame centre — deliberate re-aiming switches, jitter does not.
        const takeover = RIVAL_TAKEOVER_DIST_FRAC ** 2 * distSq[current];
        winner = distSq[nearest] < takeover ? nearest : current;
      } else {
        // Tracked card missed: rivals wait until tracking is lost
        // (anti-bounce), except a dead-centre one — take it immediately.
        const radius = DEAD_CENTER_RADIUS_FRAC * Math.min(frameW, frameH);
        winner = distSq[nearest] < radius * radius ? nearest : -1;
      }
    }

    if (winner < 0) return [];
    this.remember(items[winner].box, now);
    return [items[winner]];
  }

  /** Index of the item overlapping the tracked box the most, or -1. */
  private matchTracked(items: readonly Trackable[]): number {
    const tracked = this.tracked?.box;
    if (!tracked) return -1;
    let best = -1;
    let bestIou = SAME_CARD_MIN_IOU;
    items.forEach((it, i) => {
      const overlap = iou(tracked, it.box);
      if (overlap >= bestIou) {
        bestIou = overlap;
        best = i;
      }
    });
    return best;
  }

  private remember(box: Box, now: number): void {
    this.tracked = { box, lastSeen: now };
  }
}
