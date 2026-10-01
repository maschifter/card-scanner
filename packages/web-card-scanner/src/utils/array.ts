/** The item with the smallest key; throws on an empty array. */
export function argMin<T>(items: readonly T[], key: (item: T) => number): T {
  return items.reduce((best, item) => (key(item) < key(best) ? item : best));
}

/** The item with the largest key; throws on an empty array. */
export function argMax<T>(items: readonly T[], key: (item: T) => number): T {
  return items.reduce((best, item) => (key(item) > key(best) ? item : best));
}
