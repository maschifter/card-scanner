import type { InferenceResult } from './index';

export function runInference(modelPath: string): InferenceResult {
  return global.runInference(modelPath);
}
