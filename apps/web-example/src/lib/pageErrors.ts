/** Report uncaught page errors and unhandled rejections through `report`
 *  (Safari especially fails silently otherwise). Returns the unsubscribe. */
export function reportPageErrors(report: (message: string) => void) {
  const onError = (e: ErrorEvent) => report(`page error: ${e.message}`);
  const onRejection = (e: PromiseRejectionEvent) =>
    report(`unhandled rejection: ${e.reason}`);
  window.addEventListener('error', onError);
  window.addEventListener('unhandledrejection', onRejection);
  return () => {
    window.removeEventListener('error', onError);
    window.removeEventListener('unhandledrejection', onRejection);
  };
}
