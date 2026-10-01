#!/usr/bin/env node
// Rebuilds the plugin into a staging directory and zips it, with install notes,
// onto the Desktop: a bundle to hand to someone who does not build it.
import { existsSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { homedir } from 'node:os';
import path from 'node:path';
import {
  REPO_DIR, IS_MAC, IS_WIN, fail, step, run, versionOf,
  parseArgs, preflight, buildOverlay, configure, build, runTests, assembleBundle, verifyServer,
} from './release.mjs';

const USAGE = `usage: node tools/export.mjs [--out <zip>] [release.mjs options]

Builds exactly like release.mjs, but instead of installing into OBS assembles
the bundle in build-obs/export, verifies it there, and zips it together with
INSTALL.txt to ~/Desktop/obs-card-scanner-<version>-<commit>-<platform>.zip.
The name also marks a non-Release build type or benchmark timers.

  --out <zip>            write the archive here instead of the Desktop
`;

const BUNDLE_NAME = IS_MAC ? 'obs-card-scanner.plugin' : 'obs-card-scanner';
const PLATFORM = IS_MAC ? 'macos-arm64' : 'windows-x64';

function installNotes() {
  const shared = `Start OBS, select your camera source, open Filters, add "Card Scanner", and
click "Add overlay to current scene".
`;
  if (IS_MAC) {
    return `Card Scanner for OBS Studio - macOS 14 or newer, Apple silicon

1. Quit OBS.
2. Move ${BUNDLE_NAME} into ~/Library/Application Support/obs-studio/plugins/
   (create the plugins folder if it does not exist).
3. The bundle is not notarized. If it arrived through a browser download,
   macOS has flagged it and OBS will refuse to load it; clear the flag once:
     xattr -dr com.apple.quarantine ~/Library/Application\\ Support/obs-studio/plugins/${BUNDLE_NAME}
4. ${shared}`;
  }
  return `Card Scanner for OBS Studio - Windows 11, 64-bit

1. Close OBS.
2. Move the ${BUNDLE_NAME} folder into %ProgramData%\\obs-studio\\plugins\\
   (create the plugins folder if it does not exist).
3. Needs the Visual C++ 2015-2022 runtime, which the OBS installer already
   requires, and DirectX 12 for GPU acceleration (otherwise the CPU is used).
   The files are not signed; if Windows blocks them, unblock the folder in
   PowerShell:  Get-ChildItem -Recurse "$env:ProgramData\\obs-studio\\plugins\\${BUNDLE_NAME}" | Unblock-File
4. ${shared}`;
}

function zipName(opts) {
  const { version } = JSON.parse(
    readFileSync(path.join(REPO_DIR, 'packages', 'desktop-card-scanner', 'package.json'), 'utf8'));
  const commit = versionOf('git', ['-C', REPO_DIR, 'describe', '--always', '--dirty'], /(\S+)/) ?? 'unknown';
  const variant = (opts.config === 'Release' ? '' : `-${opts.config.toLowerCase()}`)
    + (opts.benchmark ? '-benchmark' : '');
  return `obs-card-scanner-${version}-${commit}-${PLATFORM}${variant}.zip`;
}

// --- Main ---

const argv = process.argv.slice(2);
if (argv.includes('-h') || argv.includes('--help')) {
  console.log(USAGE);
  process.exit(0);
}
let out = null;
const outAt = argv.indexOf('--out');
if (outAt !== -1) {
  out = argv[outAt + 1];
  if (!out) fail('--out needs a value');
  argv.splice(outAt, 2);
}
const opts = parseArgs(argv);
const exportDir = path.join(opts.buildDir, 'export');
opts.pluginDest = path.join(exportDir, BUNDLE_NAME);

preflight(opts);
buildOverlay(opts);
configure(opts);
build(opts);
runTests(opts);

step('Assemble the bundle');
rmSync(exportDir, { recursive: true, force: true });
assembleBundle(opts);
verifyServer(opts.pluginDest);

step('Zip');
writeFileSync(path.join(exportDir, 'INSTALL.txt'), installNotes());
const zip = path.resolve(out ?? path.join(homedir(), 'Desktop', zipName(opts)));
if (existsSync(zip)) rmSync(zip);
// bsdtar ships with both macOS and Windows and picks the format from the name.
// On Windows it is named by full path: a Git install can put GNU tar first on
// PATH, which takes "C:" for a remote host and knows no zip.
const tar = IS_WIN ? path.join(process.env.SystemRoot ?? 'C:\\Windows', 'System32', 'tar.exe') : 'tar';
run(tar, ['-a', '-cf', zip, '-C', exportDir, 'INSTALL.txt', BUNDLE_NAME]);
console.log(`\nExported: ${zip} (${Math.round(statSync(zip).size / 1048576)} MB)`);
