import { useCallback, useLayoutEffect, useRef } from 'react';
import { useFocusEffect } from '@react-navigation/native';
import {
  cardScannerPlugin,
  type CardScannerFramePlugin,
} from '@cardnexus/card-scanner';

type DetectionListener = Parameters<
  CardScannerFramePlugin['setDetectionListener']
>[0];

let latestClaimId = 0;

/** Claims the plugin's single listener slot on focus and releases it once no
 *  screen holds a claim. */
export function useDetectionListener(listener: DetectionListener) {
  const listenerRef = useRef(listener);
  useLayoutEffect(() => {
    listenerRef.current = listener;
  });

  useFocusEffect(
    useCallback(() => {
      const claimId = ++latestClaimId;
      cardScannerPlugin.setDetectionListener((res) => listenerRef.current(res));
      return () => {
        if (latestClaimId === claimId) {
          cardScannerPlugin.clearDetectionListener();
        }
      };
    }, []),
  );
}
