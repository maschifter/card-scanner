// Build step of release.mjs and Metro (off with --no-manifest / USE_MANIFEST=0): takes models and databases from the CDN manifest.
// node tools/manifest.mjs <mobile|desktop> <dir> writes them into <dir> plus from-manifest.json listing what was taken.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const CDN = 'https://mobile-data-cdn.cardnexus.com';
const REPO_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

// Manifest role -> format -> path in <dir>, the same path the build reads its repo copy from.
const MODEL_FILES = {
  mobile: {
    classifier: { coreml: 'CardSegmentationModel.coreml.pte', pte: 'CardSegmentationModel.xnnpack.pte' },
    embedder: { pte: 'CardRecognitionModel.pte' },
    set_symbols_classifier: {
      coreml: 'mtg/SetSymbolDetectionModel.coreml.pte', pte: 'mtg/SetSymbolDetectionModel.xnnpack.pte',
    },
    set_symbols_embedder: { pte: 'mtg/SetSymbolRecognitionModel.pte' },
    fab_classifier: { pte: 'fab/ColorBarModel.pte' },
    language_classifier: { coreml: 'CardLanguageModel.coreml.pte', 'pte-int8': 'CardLanguageModel.xnnpack.pte' },
    set_code_reader: { pte: 'ygo/SetCodeReader.pte' },
  },
  desktop: {
    classifier: { onnx: 'assets/models/CardSegmentationModel.onnx' },
    embedder: { onnx: 'assets/models/CardRecognitionModel.onnx' },
    set_symbols_classifier: { onnx: 'assets/models/SetSymbolDetectionModel.onnx' },
    set_symbols_embedder: { onnx: 'assets/models/SetSymbolRecognitionModel.onnx' },
    fab_classifier: { onnx: 'assets/models/ColorBarModel.onnx' },
    language_classifier: { onnx: 'assets/models/CardLanguageModel.onnx' },
    set_code_reader: { onnx: 'assets/models/SetCodeReader.onnx' },
  },
};

const DATABASE_FILES = {
  mobile: { game: (game) => `${game}.mdb`, setSymbols: 'mtg/mtg-sets.mdb' },
  desktop: { game: (game) => `assets/databases/${game}/data.mdb`, setSymbols: 'assets/databases/set-symbols/data.mdb' },
};

async function main(platform, dir) {
  const manifest = await fetchManifest();
  const fromManifest = { files: [] };
  for (const [role, formats] of Object.entries(manifest ? MODEL_FILES[platform] : {})) {
    const choice = choose(role, formats, manifest, platform, fromManifest.gameClassMapping);
    if (choice.reason) {
      console.log(`repo:     ${role} (${choice.reason})`);
      continue;
    }
    const downloads = [...choice.files, ...choice.databases];
    try {
      for (const file of downloads) await download(file, dir);
      if (CLASS_COUNT[role]) checkClassCount(role, choice, dir);
    } catch (e) {
      console.log(`repo:     ${role} (${e.message})`);
      continue;
    }
    fromManifest.files.push(...downloads.map((file) => file.target));
    Object.assign(fromManifest, choice.classNames);
    // Changes with the databases, so the phone replaces the ones it installed earlier
    if (role === 'embedder') {
      fromManifest.dbBundleVersion = sha256(choice.databases.map(manifestSha).join()).slice(0, 16);
    }
    const extra = choice.databases.length ? ` + ${choice.databases.length} databases` : '';
    console.log(`manifest: ${role} v${choice.version.version}${extra}`);
  }
  // Written even when nothing was taken, so a previous build's list never applies
  mkdirSync(dir, { recursive: true });
  writeFileSync(path.join(dir, 'from-manifest.json'), `${JSON.stringify(fromManifest, null, 2)}\n`);
}

// null when the CDN cannot be reached: the build then keeps every repo copy.
async function fetchManifest() {
  try {
    const res = await fetch(`${CDN}/manifest-v2.json`);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return await res.json();
  } catch (e) {
    console.log(`manifest unavailable (${e.message}): everything comes from the repo`);
    return null;
  }
}

// The version, files, class names and databases a model takes from the manifest, or why it keeps the repo copy.
function choose(role, formats, manifest, platform, gameClassMapping) {
  const live = (manifest.models[role] ?? []).filter((version) => version.live);
  if (!live.length) return { reason: 'no live version' };
  // A version without a detail was not made by a training job, like the CDN's mock models
  const version = live.filter((v) => v.detail).sort((a, b) => b.version - a.version)[0];
  if (!version) return { reason: 'no detail' };
  const missing = Object.keys(formats).filter((format) => !version.formats[format]);
  if (missing.length) return { reason: `no ${missing.join(' or ')} file` };
  const labels = role === 'language_classifier' ? version.detail.labels?.card_probs : version.detail.labels;
  const names = classNames(role, labels, manifest.labels);
  if (!names) return { reason: 'no class names in its detail' };
  const databases = databasesFor(role, version, manifest, platform, gameClassMapping);
  if (!databases.every(Boolean)) return { reason: `no v${version.version} database for every game` };
  const files = Object.entries(formats).map(([format, target]) => ({ target, ...version.formats[format] }));
  return { version, files, labels, classNames: names, databases };
}

// The scanner config keys for a model's class order: {} when it has none, null when its detail lacks it.
function classNames(role, labels, registry) {
  switch (role) {
    case 'classifier': {
      if (!Array.isArray(labels)) return null;
      const games = labels.map((label) => registry.find((entry) => entry.label === label)?.gameSlugs);
      return games.every((slugs) => slugs?.length) ? { gameClassMapping: Object.fromEntries(games.entries()) } : null;
    }
    case 'language_classifier':
      return Array.isArray(labels) ? { languageClassNames: labels } : null;
    case 'fab_classifier':
      return Array.isArray(labels) ? { colorDetection: { classNames: labels } } : null;
    case 'set_code_reader':
      return Array.isArray(labels) ? { setCodeReading: { classNames: labels } } : null;
    default:
      return {};
  }
}

const CLASS_COUNT = {
  // YOLO segmentation: [1, 4 box + classes + 32 mask coefficients, anchors]
  classifier: (shape) => shape.at(-2) - 36,
  language_classifier: (shape) => shape.at(-1),
  fab_classifier: (shape) => shape.at(-1),
  set_code_reader: (shape) => shape.at(-1),
};

function checkClassCount(role, { files, labels }, dir) {
  for (const { target } of files) {
    const bytes = readFileSync(path.join(dir, target));
    const classes = CLASS_COUNT[role](target.endsWith('.onnx') ? onnxOutputShape(bytes) : pteOutputShape(bytes));
    if (classes !== labels.length) throw new Error(`${target} outputs ${classes} classes for ${labels.length} labels`);
  }
}

// onnx.proto field numbers of ModelProto.graph.output[0].type.tensor_type.shape.dim[].dim_value
function onnxOutputShape(bytes) {
  const first = (message, number) => protobufFields(message).find(([n]) => n === number)?.[1];
  const shape = [7, 12, 2, 1, 2].reduce(first, bytes);
  return protobufFields(shape).filter(([n]) => n === 1).map(([, dim]) => first(dim, 1));
}

function protobufFields(bytes) {
  const fields = [];
  let pos = 0;
  const varint = () => {
    let value = 0;
    for (let scale = 1; ; scale *= 128) {
      const byte = bytes[pos++];
      value += (byte & 0x7f) * scale;
      if ((byte & 0x80) === 0) return value;
    }
  };
  while (pos < bytes.length) {
    const key = varint();
    const number = Math.floor(key / 8);
    if (key % 8 === 0) fields.push([number, varint()]);
    else if (key % 8 === 2) {
      const length = varint();
      fields.push([number, bytes.subarray(pos, (pos += length))]);
    } else pos += key % 8 === 1 ? 8 : 4;
  }
  return fields;
}

// ExecuTorch program.fbs field positions of Program.execution_plan[0].values[outputs[0]].val (a union: 1).sizes
function pteOutputShape(bytes) {
  const deref = (at) => at + bytes.readUInt32LE(at);
  const field = (table, index) => deref(table + bytes.readUInt16LE(table - bytes.readInt32LE(table) + 4 + 2 * index));
  const element = (vector, i) => vector + 4 + 4 * i;
  const plan = deref(element(field(deref(0), 1), 0));
  const output = bytes.readInt32LE(element(field(plan, 4), 0));
  const sizes = field(field(deref(element(field(plan, 2), output)), 1), 2);
  return Array.from({ length: bytes.readUInt32LE(sizes) }, (_, i) => bytes.readInt32LE(element(sizes, i)));
}

// An embedder only matches databases it built: one per game its classifier routes to, undefined where the manifest has none.
function databasesFor(role, version, manifest, platform, gameClassMapping) {
  const signature = (game, list) =>
    manifest.games.find((g) => g.game === game)?.[list].find((s) => s.embedderVersion === version.version);
  if (role === 'embedder') {
    const games = gameClassMapping ? Object.values(gameClassMapping).flat() : repoGames(platform);
    return games.map((game) => {
      const db = signature(game, 'signatures');
      return db && { target: DATABASE_FILES[platform].game(game), ...db };
    });
  }
  if (role === 'set_symbols_embedder') {
    const db = signature('mtg', 'setSymbolSignatures');
    return [db && { target: DATABASE_FILES[platform].setSymbols, ...db }];
  }
  return [];
}

// The games the repo ships a database for.
function repoGames(platform) {
  if (platform === 'mobile') {
    return readdirSync(path.join(REPO_DIR, 'apps/mobile-example/assets'))
      .filter((file) => file.endsWith('.mdb')).map((file) => path.basename(file, '.mdb'));
  }
  const dir = path.join(REPO_DIR, 'apps/desktop-example/assets/databases');
  return readdirSync(dir).filter((game) => game !== 'set-symbols' && existsSync(path.join(dir, game, 'data.mdb')));
}

// Reuses a file an earlier build left in place; a download whose sha256 differs from the manifest is rejected.
async function download(file, dir) {
  const target = path.join(dir, file.target);
  if (existsSync(target) && sha256(readFileSync(target)) === manifestSha(file)) return;
  const res = await fetch(CDN + file.path).catch((e) => { throw new Error(`${file.path}: ${e.message}`); });
  if (!res.ok) throw new Error(`${file.path}: HTTP ${res.status}`);
  const body = Buffer.from(await res.arrayBuffer());
  if (sha256(body) !== manifestSha(file)) throw new Error(`${file.path}: sha256 differs from the manifest`);
  mkdirSync(path.dirname(target), { recursive: true });
  writeFileSync(target, body);
}

// Database entries carry a "sha256:" prefix, model files do not
const manifestSha = (file) => file.sha256.replace(/^sha256:/, '');
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');

const [platform, dir] = process.argv.slice(2);
if (!MODEL_FILES[platform] || !dir) {
  console.error('usage: node tools/manifest.mjs <mobile|desktop> <dir>');
  process.exit(1);
}
await main(platform, path.resolve(dir));
