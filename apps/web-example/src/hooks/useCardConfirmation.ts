/** Card identity debounce — the mobile example's useCardConfirmation. A card
 *  lands in the history only after the same cardId is read on consecutive
 *  frames: 2 normally, 4 when the top two matches are within 1% or an MTG
 *  card has no set symbol yet. Set-symbol reads collected while waiting are
 *  resolved to the best one. Counters live in refs; only the history and
 *  the progress indicator render. */

import { useCallback, useRef, useState } from 'react';
import type { CardView } from './useScanLoop.ts';

export interface ConfirmedCard {
  cardId: string;
  name: string;
  gameName: string;
  score: number;
  setSymbol: CardView['setSymbol'];
  fabColor: CardView['fabColor'];
  language: CardView['language'];
  /** Copy of the confirming frame's dewarp (the live thumb gets recycled). */
  thumb: HTMLCanvasElement;
}

export interface ConfirmProgress {
  cardId: string;
  name: string;
  count: number;
  required: number;
}

interface Pending {
  count: number;
  setSymbol: CardView['setSymbol'];
}

const HISTORY_LIMIT = 20;
const CLOSE_MATCH_MARGIN = 0.01;

function snapshot(thumb: ImageBitmap): HTMLCanvasElement {
  const c = document.createElement('canvas');
  c.width = thumb.width;
  c.height = thumb.height;
  c.getContext('2d')!.drawImage(thumb, 0, 0);
  return c;
}

export function useCardConfirmation() {
  const [history, setHistory] = useState<ConfirmedCard[]>([]);
  const [progress, setProgress] = useState<ConfirmProgress[]>([]);
  const pendingRef = useRef(new Map<string, Pending>());
  const confirmedRef = useRef(new Set<string>());

  const confirm = useCallback((cards: readonly CardView[]) => {
    const pending = new Map<string, Pending>();
    const counting: ConfirmProgress[] = [];
    const entries: ConfirmedCard[] = [];
    for (const card of cards) {
      const [top, runnerUp] = card.matches;
      if (!top || confirmedRef.current.has(top.cardId)) continue;
      const previous = pendingRef.current.get(top.cardId);
      const count = (previous?.count ?? 0) + 1;
      const best = previous?.setSymbol;
      const setSymbol =
        best && (!card.setSymbol || best.score >= card.setSymbol.score)
          ? best
          : card.setSymbol;
      const closeMatches =
        runnerUp !== undefined &&
        top.score - runnerUp.score <= CLOSE_MATCH_MARGIN;
      const required =
        (top.gameName === 'mtg' && !setSymbol) || closeMatches ? 4 : 2;
      const name = top.name ?? top.cardId;
      if (count < required) {
        pending.set(top.cardId, { count, setSymbol });
        counting.push({ cardId: top.cardId, name, count, required });
        continue;
      }
      confirmedRef.current.add(top.cardId);
      entries.push({
        cardId: top.cardId,
        name,
        gameName: top.gameName,
        score: top.score,
        setSymbol,
        fabColor: card.fabColor,
        language: card.language,
        thumb: snapshot(card.thumb),
      });
    }
    if (counting.length === 0 && entries.length === 0) return;
    pendingRef.current = pending;
    setProgress(counting);
    if (entries.length === 0) return;
    setHistory((prev) => {
      const kept = [...entries, ...prev].slice(0, HISTORY_LIMIT);
      confirmedRef.current = new Set(kept.map((c) => c.cardId));
      return kept;
    });
  }, []);

  const remove = useCallback((cardId: string) => {
    confirmedRef.current.delete(cardId);
    setHistory((prev) => prev.filter((c) => c.cardId !== cardId));
  }, []);

  const clear = useCallback(() => {
    confirmedRef.current.clear();
    setHistory([]);
  }, []);

  return { history, progress, confirm, remove, clear };
}
