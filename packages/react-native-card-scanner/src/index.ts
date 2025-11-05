import { CardScannerInstallerNativeModule } from './RnCardScannerModules';

export interface InferenceResult {
  outputShape: number[];
  inferenceTimeMs: number;
}

// eslint-disable-next-line no-var
declare global {
  var runInference: (modelPath: string) => InferenceResult;
}

if (global.runInference == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      `Failed to install react-native-card-scanner: The native module could not be found.`
    );
  }
  CardScannerInstallerNativeModule.install();

  if (global.runInference == null) {
    throw new Error(
      `Failed to install react-native-card-scanner: The global 'runInference' function was not found after installation.`
    );
  }
}

export { runInference } from './CardScanner';
