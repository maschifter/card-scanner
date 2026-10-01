#!/usr/bin/env node
// The OBS plugin from a clean checkout to an installed bundle: overlay, CMake
// configure and build, tests, install, then a check that the bundle starts.
import { spawnSync } from 'node:child_process';
import { closeSync, existsSync, openSync, readFileSync, readSync, rmSync } from 'node:fs';
import { availableParallelism, homedir, platform } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `usage: node tools/release.mjs [options]

Builds the overlay and the plugin, fetching and compiling every dependency,
runs the package test, installs the bundle into OBS and verifies that the
installed server starts. Tools the build cannot fetch are checked first, and
a missing one is reported with the install command for this platform.

  --config <Release|RelWithDebInfo|Debug>  build type (default Release)
  --benchmark            compile core's stage timers in
  --benchmark-verbose    the timers plus the per-iteration log rows
  --build-dir <dir>      default: build-obs beside this app
  --obs-version <v>      OBS to take headers from; not newer than the OBS installed
  --obs-install-dir <d>  Windows: where obs.dll lives, when not the default
  -D <KEY=VALUE>         any further CMake cache entry, repeatable
  --jobs <n>             parallel build jobs (default: all cores)
  --skip-web             reuse web/dist/index.html instead of rebuilding it
  --skip-tests           do not run ctest before installing
  --no-install           stop after build and tests
  --clean                remove the build directory first
`;

const APP_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const REPO_DIR = path.resolve(APP_DIR, '..', '..');
const WEB_DIR = path.join(APP_DIR, 'web');
const IS_WIN = platform() === 'win32';
const IS_MAC = platform() === 'darwin';
// Where OBS loads the plugin from; mirrors the PLUGIN_DEST default in CMakeLists.txt.
const OBS_PLUGIN_DIR = IS_MAC
  ? path.join(homedir(), 'Library', 'Application Support', 'obs-studio', 'plugins',
              'obs-card-scanner.plugin')
  : path.join(process.env.ProgramData ?? 'C:/ProgramData', 'obs-studio', 'plugins',
              'obs-card-scanner');

function serverIn(pluginDir) {
  return IS_MAC
    ? path.join(pluginDir, 'Contents', 'Resources', 'card-scanner-server')
    : path.join(pluginDir, 'data', 'card-scanner-server.exe');
}

// --- Process helpers ---

function fail(message) {
  console.error(`release: ${message}`);
  process.exit(1);
}

function step(title) {
  console.log(`\n==> ${title}`);
}

function run(cmd, args, options = {}) {
  console.log(`$ ${[cmd, ...args].join(' ')}`);
  const result = spawnSync(cmd, args, { stdio: 'inherit', ...options });
  if (result.error) fail(`${cmd}: ${result.error.message}`);
  if (result.status !== 0) fail(`${cmd} exited with ${result.status ?? result.signal}`);
}

function capture(cmd, args, options = {}) {
  return spawnSync(cmd, args, { encoding: 'utf8', ...options });
}

function onPath(cmd) {
  return capture(IS_WIN ? 'where' : 'which', [cmd]).status === 0;
}

function versionOf(cmd, args, pattern) {
  const result = capture(cmd, args);
  const match = result.status === 0 ? `${result.stdout}${result.stderr}`.match(pattern) : null;
  return match ? match[1] : null;
}

function atLeast(version, minimum) {
  const [major, minor] = version.split('.').map(Number);
  const [minMajor, minMinor] = minimum.split('.').map(Number);
  return major > minMajor || (major === minMajor && minor >= minMinor);
}

// --- Arguments ---

function parseArgs(argv) {
  const opts = {
    config: 'Release',
    benchmark: false,
    benchmarkVerbose: false,
    buildDir: path.join(APP_DIR, 'build-obs'),
    pluginDest: OBS_PLUGIN_DIR,
    obsVersion: null,
    obsInstallDir: null,
    defines: [],
    jobs: availableParallelism(),
    skipWeb: false,
    skipTests: false,
    install: true,
    clean: false,
  };
  const takeValue = (flag) => {
    const value = argv.shift();
    if (value === undefined) fail(`${flag} needs a value`);
    return value;
  };
  // PLUGIN_DEST is the script's own: keep it out of defines so verifyServer follows the bundle.
  const define = (entry) => {
    if (entry.startsWith('PLUGIN_DEST=')) opts.pluginDest = path.resolve(entry.slice('PLUGIN_DEST='.length));
    else opts.defines.push(entry);
  };
  while (argv.length) {
    const arg = argv.shift();
    switch (arg) {
      case '--config': opts.config = takeValue(arg); break;
      case '--benchmark': opts.benchmark = true; break;
      case '--benchmark-verbose': opts.benchmark = true; opts.benchmarkVerbose = true; break;
      case '--build-dir': opts.buildDir = path.resolve(takeValue(arg)); break;
      case '--obs-version': opts.obsVersion = takeValue(arg); break;
      case '--obs-install-dir': opts.obsInstallDir = takeValue(arg); break;
      case '-D': define(takeValue(arg)); break;
      case '--jobs': opts.jobs = Number(takeValue(arg)); break;
      case '--skip-web': opts.skipWeb = true; break;
      case '--skip-tests': opts.skipTests = true; break;
      case '--no-install': opts.install = false; break;
      case '--clean': opts.clean = true; break;
      case '-h': case '--help': console.log(USAGE); process.exit(0);
      default:
        if (arg.startsWith('-D')) { define(arg.slice(2)); break; }
        fail(`unknown argument ${arg}`);
    }
  }
  if (!['Release', 'RelWithDebInfo', 'Debug'].includes(opts.config)) {
    fail(`--config must be Release, RelWithDebInfo or Debug, not ${opts.config}`);
  }
  if (!Number.isInteger(opts.jobs) || opts.jobs < 1) fail('--jobs needs a positive integer');
  if (!IS_MAC && !IS_WIN) fail('the OBS plugin is built for macOS and Windows only');
  return opts;
}

// --- Preflight ---

function cmakeCacheEntry(buildDir, name) {
  const cache = path.join(buildDir, 'CMakeCache.txt');
  if (!existsSync(cache)) return null;
  const match = readFileSync(cache, 'utf8').match(new RegExp(`^${name}:[A-Z]+=(.*)$`, 'm'));
  return match ? match[1] : null;
}

// An LFS pointer file is text starting with a version line; a real model is not.
function modelsArePresent() {
  const probe = path.join(APP_DIR, 'assets', 'models', 'CardSegmentationModel.onnx');
  if (!existsSync(probe)) return false;
  const fd = openSync(probe, 'r');
  const head = Buffer.alloc(24);
  readSync(fd, head, 0, head.length, 0);
  closeSync(fd);
  return !head.toString('latin1').startsWith('version https://git-lfs');
}

// Yarn ships in the repo; running its release file needs no corepack.
function yarnPath() {
  const line = readFileSync(path.join(REPO_DIR, '.yarnrc.yml'), 'utf8')
    .split('\n').find((l) => l.startsWith('yarnPath:'));
  if (!line) fail('.yarnrc.yml has no yarnPath');
  const yarn = path.join(REPO_DIR, line.slice('yarnPath:'.length).trim());
  if (!existsSync(yarn)) fail(`${yarn} is missing`);
  return yarn;
}

// Checks every tool at once and lists the gaps with install commands; installs nothing.
function preflight(opts) {
  step('Preflight');
  const have = [];
  const missing = [];
  const check = (name, version, fix, ok = Boolean(version)) => {
    if (ok) have.push(`${name} ${version}`);
    else missing.push(`${name}${version ? ` (found ${version})` : ''}: ${fix}`);
  };

  const nodeVersion = process.versions.node;
  check('node', nodeVersion,
    `Node 20 or newer: ${IS_MAC ? 'brew install node@22' : 'winget install OpenJS.NodeJS.LTS'}`,
    Number(nodeVersion.split('.')[0]) >= 20);
  check('git', versionOf('git', ['--version'], /git version (\S+)/),
    IS_MAC ? 'xcode-select --install' : 'winget install Git.Git');
  check('git-lfs', versionOf('git', ['lfs', 'version'], /git-lfs\/(\S+)/),
    IS_MAC ? 'brew install git-lfs && git lfs install'
           : 'winget install Git.Git (it ships Git LFS), then: git lfs install');
  const cmakeVersion = versionOf('cmake', ['--version'], /cmake version (\d+\.\d+\.\d+)/);
  check('cmake', cmakeVersion,
    `CMake 3.26 or newer: ${IS_MAC ? 'brew install cmake'
      : 'tick "C++ CMake tools for Windows" in the Visual Studio Installer, or: winget install Kitware.CMake'}`,
    cmakeVersion !== null && atLeast(cmakeVersion, '3.26'));
  if (IS_MAC) {
    const clt = capture('xcode-select', ['-p']);
    check('Xcode command line tools', clt.status === 0 ? clt.stdout.trim() : null, 'xcode-select --install');
  } else {
    check('MSVC cl.exe', onPath('cl') ? 'on PATH' : null,
      'install Visual Studio 2022 Build Tools with the "Desktop development with C++" workload, '
      + 'then run this from the "x64 Native Tools Command Prompt for VS 2022"');
    // The module links an import library derived from the installed obs.dll.
    const obsDir = opts.obsInstallDir ?? cmakeCacheEntry(opts.buildDir, 'OBS_INSTALL_DIR')
      ?? 'C:/Program Files/obs-studio';
    check('OBS Studio', existsSync(path.join(obsDir, 'bin', '64bit', 'obs.dll')) ? obsDir : null,
      'winget install OBSProject.OBSStudio, or pass --obs-install-dir <dir>');
    if (!onPath('ninja')) {
      console.log('note: ninja is not on PATH, so CMake falls back to a slower generator '
        + '(NMake in a vcvars shell, single-threaded); '
        + '"C++ CMake tools for Windows" in the Visual Studio Installer adds it');
    }
  }
  check('models and databases (Git LFS)', modelsArePresent() ? 'present' : null,
    'git lfs install && git lfs pull');

  console.log(`tools      ${have.join(', ')}`);
  if (missing.length) {
    console.error(`\nrelease: this ${IS_MAC ? 'Mac' : 'Windows machine'} lacks what the build needs. Install, then rerun:`);
    for (const item of missing) console.error(`  - ${item}`);
    process.exit(1);
  }
  console.log(`app        ${APP_DIR}`);
  console.log(`build dir  ${opts.buildDir}`);
  console.log(`config     ${opts.config}${opts.benchmark ? ' + benchmark timers' : ''}`);
}

// --- Steps ---

function buildOverlay(opts) {
  const overlay = path.join(WEB_DIR, 'dist', 'index.html');
  if (opts.skipWeb) {
    if (!existsSync(overlay)) fail(`--skip-web, but ${overlay} does not exist`);
    return;
  }
  step('Overlay');
  const yarn = yarnPath();
  run(process.execPath, [yarn, 'install', '--immutable'], { cwd: WEB_DIR });
  run(process.execPath, [yarn, 'build'], { cwd: WEB_DIR });
}

function configure(opts) {
  if (opts.clean && existsSync(opts.buildDir)) {
    step('Clean');
    rmSync(opts.buildDir, { recursive: true, force: true });
  }
  step('Configure');
  // PLUGIN_DEST always: a -D value stays cached, and export.mjs points it elsewhere.
  const args = [
    '-S', APP_DIR, '-B', opts.buildDir,
    '-DBUILD_OBS_MODULE=ON',
    `-DPLUGIN_DEST=${opts.pluginDest}`,
    `-DCMAKE_BUILD_TYPE=${opts.config}`,
    `-DCARDSCANNER_BENCHMARK=${opts.benchmark ? 1 : 0}`,
    `-DCARDSCANNER_BENCHMARK_VERBOSE=${opts.benchmarkVerbose ? 1 : 0}`,
  ];
  // A build directory keeps its generator, so only a fresh one picks Ninja.
  if (IS_WIN && !existsSync(path.join(opts.buildDir, 'CMakeCache.txt')) && onPath('ninja')) {
    args.push('-G', 'Ninja');
  }
  if (opts.obsVersion) args.push(`-DOBS_VERSION=${opts.obsVersion}`);
  if (opts.obsInstallDir) args.push(`-DOBS_INSTALL_DIR=${opts.obsInstallDir}`);
  for (const define of opts.defines) args.push(`-D${define}`);
  run('cmake', args);
}

function build(opts) {
  step('Build');
  run('cmake', ['--build', opts.buildDir, '--config', opts.config, '--parallel', String(opts.jobs)]);
}

// The test is registered in the package's directory, not the app's.
function runTests(opts) {
  if (opts.skipTests) return;
  step('Tests');
  run('ctest', ['--test-dir', path.join(opts.buildDir, 'cardscanner_desktop'),
                '-C', opts.config, '--output-on-failure']);
}

// Assembles module, server, libraries, assets and overlay into opts.pluginDest.
function assembleBundle(opts) {
  run('cmake', ['--build', opts.buildDir, '--config', opts.config, '--target', 'install-obs-plugin']);
}

function installIntoObs(opts) {
  step('Install into OBS');
  if (IS_WIN) {
    // Windows will not overwrite a DLL that a process has loaded.
    const tasks = capture('tasklist', ['/FI', 'IMAGENAME eq obs64.exe', '/NH']);
    if (/obs64\.exe/i.test(tasks.stdout ?? '')) fail('OBS is running; close it before installing');
  } else if (capture('pgrep', ['-x', 'OBS']).status === 0) {
    console.log('note: OBS is running; it loads plugins at startup, so restart it afterwards');
  }
  assembleBundle(opts);
}

// --- Verification ---

// Nothing outside @rpath, /usr/lib and /System, and no rpath but @executable_path.
function verifyMacLinkage(server) {
  const loads = capture('otool', ['-L', server]).stdout.split('\n').slice(1)
    .map((line) => line.trim().split(' ')[0]).filter(Boolean);
  const foreign = loads.filter((lib) =>
    !lib.startsWith('@rpath/') && !lib.startsWith('/usr/lib/') && !lib.startsWith('/System/'));
  if (foreign.length) fail(`server links libraries outside the bundle:\n  ${foreign.join('\n  ')}`);
  const rpaths = [...capture('otool', ['-l', server]).stdout.matchAll(/^\s+path (\S+)/gm)]
    .map((m) => m[1]);
  const stray = rpaths.filter((p) => p !== '@executable_path');
  if (stray.length) fail(`server carries rpaths beyond @executable_path:\n  ${stray.join('\n  ')}`);
}

// No arguments means usage and exit 1, which needs every library beside it
// loaded; a missing one kills it before main().
function verifyServer(pluginDir) {
  step('Verify the assembled server');
  const server = serverIn(pluginDir);
  if (!existsSync(server)) fail(`${server} was not assembled`);
  if (IS_MAC) verifyMacLinkage(server);

  const smoke = capture(server, [], { cwd: path.dirname(server) });
  const dllNotFound = smoke.status === 0xC0000135 || smoke.status === -1073741515;
  if (smoke.signal || dllNotFound || /Library not loaded/.test(smoke.stderr ?? '')) {
    fail(`installed server does not start:\n${smoke.stderr}`);
  }
  if (smoke.status !== 1 || !/usage:/.test(smoke.stderr ?? '')) {
    fail(`unexpected reply from the installed server (exit ${smoke.status}):\n${smoke.stderr}`);
  }
  console.log(`\nVerified: ${path.dirname(server)}`);
}

// --- Main ---

function main() {
  const opts = parseArgs(process.argv.slice(2));
  preflight(opts);
  buildOverlay(opts);
  configure(opts);
  build(opts);
  runTests(opts);
  if (!opts.install) {
    console.log('\nBuilt and verified; --no-install, so OBS was not touched.');
    return;
  }
  installIntoObs(opts);
  verifyServer(opts.pluginDest);
  console.log('Restart OBS to load this build.');
}

// export.mjs composes the same steps around a staging directory and a zip.
export { APP_DIR, REPO_DIR, IS_MAC, IS_WIN, fail, step, run, capture, versionOf,
         parseArgs, preflight, buildOverlay, configure, build, runTests, assembleBundle, verifyServer };

if (path.resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) main();
