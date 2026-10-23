import type { ScannerConfig } from '@cardnexus/card-scanner';
import fromManifestJson from '../assets/from-manifest.json';

type ColorDetection = NonNullable<
  NonNullable<ScannerConfig['gameSpecificConfig']>[string]['colorDetection']
>;

// What the build took from the CDN manifest (metro.config.js); empty when it took nothing.
export const fromManifest: Partial<
  Pick<ScannerConfig, 'gameClassMapping' | 'languageClassNames'>
> & {
  dbBundleVersion?: string;
  colorDetection?: Pick<ColorDetection, 'classNames'>;
} = fromManifestJson;
