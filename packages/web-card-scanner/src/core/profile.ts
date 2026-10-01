import type { TgpuRoot } from 'typegpu';
import type { Runner } from '../inference/runner.ts';
import type { Runners } from './setup.ts';

export type ProfiledModel = keyof Runners;

export interface KernelProfile {
  /** The first thing a "slow on this machine" report needs. */
  adapter: string;
  totalMs: number;
  dispatches: number;
  /** Slowest first, dispatches of the same kernel merged. */
  kernels: { name: string; ms: number; dispatches: number }[];
}

/** Null when the device lacks 'timestamp-query' or the model never ran. */
export async function profileRunner(
  root: TgpuRoot,
  runner: Runner,
): Promise<KernelProfile | null> {
  const timings = await runner.profile();
  if (!timings) return null;
  const byName = new Map<string, { ms: number; dispatches: number }>();
  for (const t of timings) {
    const e = byName.get(t.name) ?? { ms: 0, dispatches: 0 };
    e.ms += t.ns / 1e6;
    e.dispatches += 1;
    byName.set(t.name, e);
  }
  const kernels = [...byName]
    .map(([name, e]) => ({ name, ...e }))
    .sort((a, b) => b.ms - a.ms);
  const info = root.device.adapterInfo;
  const parts = [info?.vendor, info?.architecture, info?.device].filter(
    Boolean,
  );
  return {
    adapter: `${parts.join(' / ') || 'unknown'} · f16 ${
      root.enabledFeatures.has('shader-f16') ? 'on' : 'OFF'
    }`,
    totalMs: kernels.reduce((s, k) => s + k.ms, 0),
    dispatches: timings.length,
    kernels,
  };
}
