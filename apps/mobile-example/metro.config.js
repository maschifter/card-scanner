const { execFileSync } = require('node:child_process');
const { existsSync, readFileSync, writeFileSync } = require('node:fs');
const path = require('node:path');
const { getDefaultConfig } = require('expo/metro-config');

const config = getDefaultConfig(__dirname);

// Add .pte as an asset extension
config.resolver.assetExts.push('pte');
config.resolver.assetExts.push('mdb');

// The CDN manifest's models and databases (tools/manifest.mjs); USE_MANIFEST=0 keeps the repo's.
if (process.env.USE_MANIFEST !== '0') {
  const assets = path.join(__dirname, 'assets');
  const fetched = path.join(__dirname, 'manifest-assets');
  execFileSync(
    process.execPath,
    [path.join(__dirname, '../../tools/manifest.mjs'), 'mobile', fetched],
    { stdio: 'inherit' },
  );
  const { files } = JSON.parse(
    readFileSync(path.join(fetched, 'from-manifest.json'), 'utf8'),
  );
  const databases = files
    .filter(
      (file) => file.endsWith('.mdb') && !existsSync(path.join(assets, file)),
    )
    .map(
      (file) =>
        `  { name: '${path.basename(file, '.mdb')}', asset: require('./${file}') },\n`,
    );
  writeFileSync(
    path.join(fetched, 'from-manifest-databases.ts'),
    `export default [\n${databases.join('')}];\n`,
  );
  // Assets the manifest supplied resolve to the downloaded copies.
  const redirect = (file) => {
    const name = path.relative(assets, file).split(path.sep).join('/');
    return files.includes(name) ||
      name === 'from-manifest.json' ||
      name === 'from-manifest-databases.ts'
      ? path.join(fetched, name)
      : file;
  };
  config.resolver.resolveRequest = (context, moduleName, platform) => {
    const resolution = context.resolveRequest(context, moduleName, platform);
    if (resolution.type === 'sourceFile') {
      return { ...resolution, filePath: redirect(resolution.filePath) };
    }
    if (resolution.type === 'assetFiles') {
      return { ...resolution, filePaths: resolution.filePaths.map(redirect) };
    }
    return resolution;
  };
}

module.exports = config;
