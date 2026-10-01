import { useCallback, useRef } from 'react';
import { useFocusEffect } from '@react-navigation/native';
import {
  resumeScanning,
  setScanMode,
  type ScanMode,
} from '@cardnexus/card-scanner';

/** Puts native in this screen's scan mode, unpaused, each time the screen
 *  gains focus. Both are process-wide: another screen may have left its own
 *  mode, or a multi-card pause nobody resumed, behind. Waits for `ready`:
 *  loading the scanner replaces its config, mode included. */
export function useScanMode(mode: ScanMode, ready: boolean) {
  // Read at focus time, so a mode picked while focused does not re-run this.
  const modeRef = useRef(mode);
  modeRef.current = mode;

  useFocusEffect(
    useCallback(() => {
      if (!ready) {
        return;
      }
      setScanMode(modeRef.current);
      resumeScanning();
    }, [ready]),
  );
}
