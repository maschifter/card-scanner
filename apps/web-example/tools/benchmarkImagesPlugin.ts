import { createReadStream, readFileSync, statSync } from 'node:fs';
import { join, sep } from 'node:path';
import type { Connect, Plugin } from 'vite';

const MOBILE_EXAMPLE = join(import.meta.dirname, '../../mobile-example');
const PHOTOS_DIR = join(MOBILE_EXAMPLE, 'assets/games_images');
const PHOTO_LIST = join(MOBILE_EXAMPLE, 'utils/benchmarkImages.ts');
const ROOT_PACKAGE = join(import.meta.dirname, '../../../package.json');

function readPhotoList() {
  const src = readFileSync(PHOTO_LIST, 'utf8').replace(/\/\/.*$/gm, '');
  return [...src.matchAll(/\{([^{}]*require\([^{}]*)\}/g)].map(([, entry]) => {
    const game = entry.match(/game:\s*'([^']+)'/)?.[1];
    const ids = entry.match(/cardIds:\s*\[([^\]]*)\]/)?.[1];
    const file = entry.match(/games_images\/([^']+)'/)?.[1];
    if (!game || !ids || !file) {
      throw new Error(`benchmark: unreadable entry in ${PHOTO_LIST}`);
    }
    const cardIds = [...ids.matchAll(/'([^']+)'/g)].map(([, id]) => id);
    return { game, cardIds, file, url: `/bench/images/${file}` };
  });
}

function addBenchmarkRoutes(middlewares: Connect.Server) {
  middlewares.use('/bench/manifest.json', (_req, res) => {
    const engine = JSON.parse(readFileSync(ROOT_PACKAGE, 'utf8')).resolutions;
    res.setHeader('content-type', 'application/json');
    res.end(
      JSON.stringify({
        source: 'apps/mobile-example/utils/benchmarkImages.ts',
        engine,
        images: readPhotoList(),
      }),
    );
  });
  middlewares.use('/bench/images/', (req, res, next) => {
    const photo = join(
      PHOTOS_DIR,
      decodeURIComponent((req.url ?? '').split('?')[0]),
    );
    const insideDir = photo.startsWith(PHOTOS_DIR + sep);
    if (!insideDir || !statSync(photo, { throwIfNoEntry: false })?.isFile()) {
      return next();
    }
    res.setHeader('content-type', 'image/jpeg');
    createReadStream(photo).pipe(res);
  });
}

export function benchmarkImagesPlugin(): Plugin {
  return {
    name: 'benchmark-images',
    configureServer: (server) => addBenchmarkRoutes(server.middlewares),
    configurePreviewServer: (server) => addBenchmarkRoutes(server.middlewares),
  };
}
