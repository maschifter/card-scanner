import { useEffect, useRef, useState } from 'react';
import {
  initialState,
  type ScanDiagnostics,
  type ScannerState,
  type StateMessage,
} from './types';

/**
 * The timings of the last measured frame, or null when the OBS filter's box
 * is off.
 */
export function useLastMeasured(scan: ScanDiagnostics): ScanDiagnostics | null {
  const [measured, setMeasured] = useState<ScanDiagnostics | null>(null);
  useEffect(() => {
    if (scan.measured) {
      setMeasured(scan);
    }
  }, [scan]);
  return scan.timings ? measured : null;
}

export type Connection = 'connecting' | 'open' | 'closed';

/**
 * Subscribes to the scanner server's control socket.
 *
 * Nothing is polled: the server pushes a full snapshot on every change and on
 * connect, so a reloading client repaints immediately. Reconnects on its own,
 * so the server can be restarted under a running OBS.
 */
export function useScannerState(port: number): {
  state: ScannerState;
  connection: Connection;
  send: (message: object) => void;
} {
  const [state, setState] = useState<ScannerState>(initialState);
  const [connection, setConnection] = useState<Connection>('connecting');
  const timer = useRef<number | undefined>(undefined);
  const live = useRef<WebSocket | undefined>(undefined);

  useEffect(() => {
    let socket: WebSocket | undefined;
    let cancelled = false;

    const connect = () => {
      if (cancelled) return;
      setConnection('connecting');
      // Captured per instance: closing over the outer variable let a late
      // error on a dead socket tear down the one that replaced it.
      const ws = new WebSocket(`ws://127.0.0.1:${port}`);
      socket = ws;
      live.current = ws;

      ws.onopen = () => setConnection('open');

      ws.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data as string) as StateMessage;
          if (message.type === 'state') setState(message.payload);
        } catch {
          // Anything we cannot parse is not ours to render.
        }
      };

      ws.onclose = () => {
        setConnection('closed');
        // Clear rather than freeze: a stale card on stream is worse than none.
        setState(initialState);
        if (!cancelled) timer.current = window.setTimeout(connect, 1500);
      };

      ws.onerror = () => ws.close();
    };

    connect();

    return () => {
      cancelled = true;
      window.clearTimeout(timer.current);
      socket?.close();
    };
  }, [port]);

  const send = (message: object) => {
    if (live.current?.readyState === WebSocket.OPEN) {
      live.current.send(JSON.stringify(message));
    }
  };

  return { state, connection, send };
}
