import type { ScannerConfig } from '@cardnexus/card-scanner';
import fromManifestJson from '../assets/from-manifest.json';

type GameConfig = NonNullable<ScannerConfig['gameSpecificConfig']>[string];
type ColorDetection = NonNullable<GameConfig['colorDetection']>;
type SetCodeReading = NonNullable<GameConfig['setCodeReading']>;

// What the build took from the CDN manifest (metro.config.js); empty when it took nothing.
export const fromManifest: Partial<
  Pick<ScannerConfig, 'gameClassMapping' | 'languageClassNames'>
> & {
  dbBundleVersion?: string;
  colorDetection?: Pick<ColorDetection, 'classNames'>;
  setCodeReading?: Pick<SetCodeReading, 'classNames'>;
} = fromManifestJson;
