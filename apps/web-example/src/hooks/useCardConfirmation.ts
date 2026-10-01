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
  /** Copy of the confirming frame's dewarp (the live thumb gets recycled). */
  thumb: HTMLCanvasElement;
}

export interface ConfirmProgress {
  cardId: string;
  name: string;
  count: number;
  required: number;
}

const HISTORY_LIMIT = 10;
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
  const [progress, setProgress] = useState<ConfirmProgress | null>(null);
  const lastIdRef = useRef<string | null>(null);
  const countRef = useRef(0);
  const symbolsRef = useRef<NonNullable<CardView['setSymbol']>[]>([]);
  const confirmedRef = useRef(new Set<string>());

  const confirm = useCallback((cards: readonly CardView[]) => {
    const card = cards.find((c) => c.matches.length > 0);
    if (!card) return;
    const [top, runnerUp] = card.matches;
    if (confirmedRef.current.has(top.cardId)) {
      setProgress(null);
      return;
    }
    const name = top.name ?? top.cardId;
    const isMtg = top.gameName === 'mtg';
    const closeMatches =
      runnerUp !== undefined &&
      top.score - runnerUp.score <= CLOSE_MATCH_MARGIN;

    if (top.cardId !== lastIdRef.current) {
      lastIdRef.current = top.cardId;
      countRef.current = 1;
      symbolsRef.current = card.setSymbol ? [card.setSymbol] : [];
      const required = (isMtg && !card.setSymbol) || closeMatches ? 4 : 2;
      setProgress({ cardId: top.cardId, name, count: 1, required });
      return;
    }

    countRef.current += 1;
    if (card.setSymbol) symbolsRef.current.push(card.setSymbol);
    const setSymbol =
      symbolsRef.current.reduce<CardView['setSymbol']>(
        (best, s) => (best && best.score >= s.score ? best : s),
        null,
      ) ?? null;
    const required = (isMtg && !setSymbol) || closeMatches ? 4 : 2;
    const count = countRef.current;
    if (count < required) {
      setProgress({ cardId: top.cardId, name, count, required });
      return;
    }

    const entry: ConfirmedCard = {
      cardId: top.cardId,
      name,
      gameName: top.gameName,
      score: top.score,
      setSymbol,
      fabColor: card.fabColor,
      thumb: snapshot(card.thumb),
    };
    confirmedRef.current.add(entry.cardId);
    setHistory((prev) => {
      const next = [entry, ...prev.filter((c) => c.cardId !== entry.cardId)];
      const kept = next.slice(0, HISTORY_LIMIT);
      confirmedRef.current = new Set(kept.map((c) => c.cardId));
      return kept;
    });
    setProgress(null);
    lastIdRef.current = null;
    countRef.current = 0;
    symbolsRef.current = [];
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
