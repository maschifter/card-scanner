import { useCallback, useRef, useState } from 'react';
import type { Detection, DetectedCard } from '@cardnexus/card-scanner';

const HISTORY_CAP = 20;

/** A history entry; `confirmed` once the user picked or confirmed it. */
export type ScannedCard = DetectedCard & { confirmed?: boolean };

/** Prepends a card to the history unless it is already there. */
const withCard = (history: ScannedCard[], card: ScannedCard) =>
  history.some((c) => c.cardId === card.cardId)
    ? history
    : [card, ...history].slice(0, HISTORY_CAP);

/**
 * Card only lands in the history after enough consecutive detections,
 * with the threshold raised for ambiguous matches.
 *
 * Extracted from VisionCameraScanner for cleanup.
 * The counters live in refs, not state - none of them are rendered.
 */
export function useCardConfirmation() {
  const [scannedCardsHistory, setScannedCardsHistory] = useState<ScannedCard[]>(
    [],
  );

  const lastScannedCardIdRef = useRef<string | null>(null);
  const consecutiveDetectionsRef = useRef(0);
  const pendingSetSymbolsRef = useRef<
    Array<{
      setCode: string;
      similarity: number;
    }>
  >([]);

  const confirmDetection = useCallback((result: Detection) => {
    if (!result.success || result.cards.length === 0) {
      return;
    }
    const firstCard = result.cards[0];

    // Check if this is a new card (different from last scanned)
    if (firstCard.cardId !== lastScannedCardIdRef.current) {
      // Different card - reset counter and start tracking
      lastScannedCardIdRef.current = firstCard.cardId ?? null;
      consecutiveDetectionsRef.current = 1; // First detection of this card

      // Reset set symbol tracking for new card
      pendingSetSymbolsRef.current = firstCard.setSymbol
        ? [firstCard.setSymbol]
        : [];

      // Don't add to history yet - wait for confirmation (2nd detection)
      return;
    }

    // Same card as before - increment counter
    const newCount = consecutiveDetectionsRef.current + 1;
    consecutiveDetectionsRef.current = newCount;

    // Collect set symbol if detected
    if (firstCard.setSymbol) {
      pendingSetSymbolsRef.current = [
        ...pendingSetSymbolsRef.current,
        firstCard.setSymbol,
      ];
    }

    // Confirmation logic:
    // - If top 2 matches are close (≤1% difference): confirm after 4 detections (need more certainty)
    // - If top match is clear winner (>1% difference): confirm after 2 detections
    const isMTG = firstCard.gameName === 'mtg';

    // Check if top 2 matches are close
    const hasCloseMatches =
      firstCard.alternativeCards &&
      firstCard.alternativeCards.length > 0 &&
      (firstCard.confidenceScore ?? 0) -
        firstCard.alternativeCards[0].confidence <=
        0.01; // 1% difference

    // Pick the best set symbol from all detections (highest similarity)
    let bestSetSymbol = firstCard.setSymbol;
    const currentSetSymbols = pendingSetSymbolsRef.current;
    if (currentSetSymbols.length > 0) {
      bestSetSymbol = currentSetSymbols.reduce((best, current) => {
        return current.similarity > best.similarity ? current : best;
      });
    }

    const hasSetSymbolMatch = bestSetSymbol?.setCode ? true : false;

    // Require 4 detections if:
    // - MTG card without set symbol OR
    // - Top 2 matches are very close (ambiguous)
    const requiredDetections =
      (isMTG && !hasSetSymbolMatch) || hasCloseMatches ? 4 : 2;

    if (newCount >= requiredDetections) {
      // Create final card with best set symbol
      const finalCard = {
        ...firstCard,
        setSymbol: bestSetSymbol,
      };

      const reason = hasCloseMatches
        ? ' (close matches)'
        : isMTG && !hasSetSymbolMatch
          ? ' (no set symbol)'
          : '';
      console.log(`Card confirmed after ${newCount} detections${reason}`);

      setScannedCardsHistory((prev) => withCard(prev, finalCard));

      pendingSetSymbolsRef.current = [];
      lastScannedCardIdRef.current = null;
      consecutiveDetectionsRef.current = 0;
    }
  }, []);

  const removeCard = useCallback((index: number) => {
    setScannedCardsHistory((prev) => prev.filter((_, i) => i !== index));
  }, []);

  /** Adds an already-confirmed card, deduped. The multi-card freeze gives
   *  one reading per card, so it skips the consecutive-detection gate. */
  const addCard = useCallback((card: DetectedCard) => {
    setScannedCardsHistory((prev) => withCard(prev, card));
  }, []);

  /** Puts the user's pick where the old entry was. A copy listed elsewhere
   *  moves there; with the old entry already deleted, the pick is added. */
  const replaceCard = useCallback((oldCardId: string, card: DetectedCard) => {
    const picked: ScannedCard = { ...card, confirmed: true };
    setScannedCardsHistory((prev) => {
      const rest = prev.filter(
        (c) => c.cardId !== card.cardId || c.cardId === oldCardId,
      );
      const at = rest.findIndex((c) => c.cardId === oldCardId);
      return at === -1
        ? [picked, ...rest].slice(0, HISTORY_CAP)
        : rest.map((c, i) => (i === at ? picked : c));
    });
  }, []);

  /** Marks a card the user confirmed as is, adding it if it is not listed. */
  const confirmCard = useCallback((card: DetectedCard) => {
    setScannedCardsHistory((prev) =>
      prev.some((c) => c.cardId === card.cardId)
        ? prev.map((c) =>
            c.cardId === card.cardId ? { ...c, confirmed: true } : c,
          )
        : [{ ...card, confirmed: true }, ...prev].slice(0, HISTORY_CAP),
    );
  }, []);

  return {
    scannedCardsHistory,
    confirmDetection,
    removeCard,
    addCard,
    replaceCard,
    confirmCard,
  };
}
