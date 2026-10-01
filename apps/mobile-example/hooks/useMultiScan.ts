import { useCallback, useEffect, useRef, useState } from 'react';
import { useFocusEffect } from '@react-navigation/native';
import {
  resumeScanning,
  type AsyncScanResult,
  type DetectedCard,
} from '@cardnexus/card-scanner';

/** One card on the frozen frame: known from `multiStart`, filled in by
 *  `multiCard`. Coordinates are pixels of the frozen image. */
export type FrozenCard = {
  placeholder: DetectedCard;
  card?: DetectedCard;
  /** The user picked or confirmed this card. */
  confirmed?: boolean;
};

export type FrozenFrame = {
  uri: string;
  width: number;
  height: number;
  cards: FrozenCard[];
  /** Every card has been through recognition. */
  done: boolean;
};

type Options = {
  /** Wait this long after a rescan before native accepts frames again, so
   *  the same layout does not re-freeze while the user is still moving. */
  cooldownMs: number;
  /** Rescan on its own this long after the last card; 0 = only on demand. */
  autoResumeMs: number;
  /** A card resolved with a match. */
  onCardResolved?: (card: DetectedCard) => void;
  /** A live frame, delivered only while no still is up. */
  onFrame: (result: AsyncScanResult) => void;
};

/** Owns the multi-card freeze: the frozen frame the UI draws, and when
 *  scanning resumes. */
export function useMultiScan({
  cooldownMs,
  autoResumeMs,
  onCardResolved,
  onFrame,
}: Options) {
  const [frozen, setFrozen] = useState<FrozenFrame | null>(null);
  // The listener runs before React re-renders, so `frozen` is stale there.
  const frozenRef = useRef(false);
  const timers = useRef<ReturnType<typeof setTimeout>[]>([]);
  const callbacks = useRef({ onCardResolved, onFrame });
  callbacks.current = { onCardResolved, onFrame };

  const clearTimers = useCallback(() => {
    timers.current.forEach(clearTimeout);
    timers.current = [];
  }, []);

  const after = useCallback((ms: number, run: () => void) => {
    timers.current.push(setTimeout(run, ms));
  }, []);

  /** Puts one card's result into its slot, from the stream or from a pick. */
  const replaceCard = useCallback(
    (index: number, card: DetectedCard, confirmed = false) => {
      setFrozen((state) =>
        state?.cards[index]
          ? {
              ...state,
              cards: state.cards.map((slot, i) =>
                i === index ? { ...slot, card, confirmed } : slot,
              ),
            }
          : state,
      );
    },
    [],
  );

  /** Hides the still now; native accepts frames again after the cooldown. */
  const rescan = useCallback(() => {
    clearTimers();
    frozenRef.current = false;
    setFrozen(null);
    after(cooldownMs, resumeScanning);
  }, [after, clearTimers, cooldownMs]);

  const onEvent = useCallback(
    (result: AsyncScanResult) => {
      switch (result.type) {
        case 'multiStart':
          clearTimers();
          frozenRef.current = true;
          setFrozen({
            uri: result.frameUri ?? '',
            width: result.frameWidth,
            height: result.frameHeight,
            cards: result.detection.cards.map((placeholder) => ({
              placeholder,
            })),
            done: false,
          });
          return;
        case 'multiCard': {
          // The rest of a page the user already rescanned away from.
          if (!frozenRef.current) {
            return;
          }
          const card = result.detection.cards[0];
          if (card && result.cardIndex !== undefined) {
            replaceCard(result.cardIndex, card);
            if (card.cardId) {
              callbacks.current.onCardResolved?.(card);
            }
          }
          return;
        }
        case 'multiEnd':
          if (!frozenRef.current) {
            return;
          }
          setFrozen((state) => state && { ...state, done: true });
          if (autoResumeMs > 0) {
            after(autoResumeMs, rescan);
          }
          return;
        default:
          if (!frozenRef.current) {
            callbacks.current.onFrame(result);
          }
      }
    },
    [after, autoResumeMs, clearTimers, rescan, replaceCard],
  );

  // A still with nothing on it (a smear, a page of card backs) is not worth
  // holding; near misses stay up for the user to confirm.
  useEffect(() => {
    if (
      frozen?.done &&
      !frozen.cards.some(
        (slot) => slot.card?.cardId || slot.card?.alternativeCards.length,
      )
    ) {
      rescan();
    }
  }, [frozen, rescan]);

  // Never leave native paused behind a screen the user walked away from.
  useFocusEffect(
    useCallback(
      () => () => {
        clearTimers();
        frozenRef.current = false;
        setFrozen(null);
        resumeScanning();
      },
      [clearTimers],
    ),
  );

  return { frozen, onEvent, rescan, replaceCard };
}
