import { CardScannerInstallerNativeModule } from './RnCardScannerModules';

// eslint-disable-next-line no-var
declare global {
  var multiply: (modelSource: string) => number;
}

if (global.multiply == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      `Failed to install react-native-card-scanner: The native module could not be found.`
    );
  }
  CardScannerInstallerNativeModule.install();

  if (global.multiply == null) {
    throw new Error(
      `Failed to install react-native-card-scanner: The global 'multiply' function was not found after installation.`
    );
  }
}

export { multiply } from './CardScanner';
