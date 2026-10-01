// The scanner process the OBS module talks to. Everything of substance lives in
// the package; this is argument parsing, a replay harness and a wait loop.
//
//   card-scanner-server <config.json> [options]
//     --token N           shared secret the module must present
//     --frame-port N      default 27846
//     --control-port N    default 27845
//     --overlay-port N    default 27847
//     --overlay <path>    defaults to overlay.html beside the config
//     --replay <dir>      pump images from a directory instead of waiting for OBS

#include <config/ScannerConfigLoader.h>
#include <net/HttpClient.h>
#include <service/ScannerServer.h>
#include <util/ParentWatchdog.h>

#include <PathProvider.h>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace desktop = cardscanner::desktop;

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

/// Replays stills as camera frames, so the path can be exercised without OBS.
void replayDirectory(const std::filesystem::path &dir, desktop::ScannerServer &server) {
  std::vector<std::filesystem::path> images;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    const auto ext = entry.path().extension().string();
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
      images.push_back(entry.path());
    }
  }
  std::sort(images.begin(), images.end());
  std::cout << "replaying " << images.size() << " image(s) from " << dir << "\n";

  while (g_running) {
    for (const auto &path : images) {
      if (!g_running) {
        return;
      }
      cv::Mat bgr = cv::imread(path.string());
      if (bgr.empty()) {
        continue;
      }
      cv::Mat rgb;
      cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
      // Held long enough to clear the stability window.
      for (int i = 0; i < 6 && g_running; i++) {
        server.submitFrame(rgb);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
      }
    }
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <config.json> [--token N] "
              << "[--frame-port N] [--control-port N] [--overlay-port N] "
              << "[--overlay <path>] [--replay <dir>]\n";
    return 1;
  }

  const std::string configPath = argv[1];
  desktop::ServerOptions options;
  std::string replayDir;

  try {
    for (int i = 2; i < argc - 1; i++) {
      const std::string flag = argv[i];
      if (flag == "--token") {
        options.token = std::stoull(argv[++i]);
      } else if (flag == "--frame-port") {
        options.framePort = uint16_t(std::stoi(argv[++i]));
      } else if (flag == "--control-port") {
        options.controlPort = uint16_t(std::stoi(argv[++i]));
      } else if (flag == "--overlay-port") {
        options.overlayPort = uint16_t(std::stoi(argv[++i]));
      } else if (flag == "--overlay") {
        options.overlayFile = argv[++i];
      } else if (flag == "--replay") {
        replayDir = argv[++i];
      }
    }
  } catch (const std::exception &) {
    // stoull/stoi otherwise throw out of main and abort with no message.
    std::cerr << "error: a numeric option was not a number\n";
    return 1;
  }

  if (options.token == 0) {
    std::random_device rd;
    options.token = (uint64_t(rd()) << 32) | rd();
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  // A write to a socket the module has closed must not kill us.
  std::signal(SIGPIPE, SIG_IGN);
  cardscanner::util::watchParent(g_running, [] { g_running = false; });

  cardscanner::net::globalInit();

  try {
    auto loaded = cardscanner::desktop::loadConfigFile(configPath);
    std::filesystem::create_directories(loaded.paths.databases);
    std::filesystem::create_directories(loaded.paths.cache);
    pathprovider::set_db_path(loaded.paths.databases.string());
    pathprovider::set_cache_path(loaded.paths.cache.string());

    if (options.overlayFile.empty()) {
      options.overlayFile =
          std::filesystem::absolute(configPath).parent_path() / "overlay.html";
    }

    std::cout << "loading models...\n";
    desktop::ScannerServer server(loaded.scanner, {}, options);
    server.start();

    std::cout << "ready\n"
              << "  frames   127.0.0.1:" << options.framePort << "  token "
              << options.token << "\n"
              << "  control  ws://127.0.0.1:" << options.controlPort << "\n"
              << "  overlay  http://127.0.0.1:" << options.overlayPort << "\n";

    if (!replayDir.empty()) {
      replayDirectory(replayDir, server);
    } else {
      while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }

    std::cout << "\nshutting down (" << server.framesReceived()
              << " frames received)\n";
    // If stop() ever stalls once OBS is gone, the orphan would hold the ports
    // until someone killed it by hand.
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      std::cerr << "shutdown stalled; exiting\n";
      std::_Exit(1);
    }).detach();
    server.stop();
    cardscanner::net::globalCleanup();
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    cardscanner::net::globalCleanup();
    return 1;
  }
}
