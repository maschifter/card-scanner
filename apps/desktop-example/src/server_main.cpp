// The scanner process the OBS module talks to. Everything of substance lives in
// the package; this is argument parsing, a replay harness and a wait loop.
//
//   card-scanner-server <config.json> [options]
//     --token N           shared secret the module must present
//     --frame-port N      default 27846
//     --control-port N    default 27845
//     --replay <dir>      pump images from a directory instead of waiting for OBS
//     --timings           report stage timings, as the OBS filter's box does

#include <Log.h>
#include <config/ScannerConfigLoader.h>
#include <http/HttpClient.h>
#include <service/ScannerServer.h>
#include <util/ParentWatchdog.h>
#include <utils/ImageUtils.h>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
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
      cv::Mat rgb;
      try {
        rgb = cardscanner::utils::ImageUtils::loadImageRGB(path.string());
      } catch (const std::exception &e) {
        cardscanner::log(cardscanner::LOG_LEVEL::Error, "[Server]",
                         "skipping unreadable image:", e.what());
        continue;
      }
      // Held long enough to clear the stability window.
      for (int i = 0; i < 6 && g_running; i++) {
        server.submitFrame({rgb});
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
      }
    }
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <config.json> [--token N] "
              << "[--frame-port N] [--control-port N] [--replay <dir>]\n";
    return 1;
  }

  const std::string configPath = argv[1];
  desktop::ServerOptions options;
  std::string replayDir;
  bool timings = false;

  try {
    for (int i = 2; i < argc; i++) {
      const std::string flag = argv[i];
      if (flag == "--timings") {
        timings = true;
      } else if (i + 1 >= argc) {
        break;
      } else if (flag == "--token") {
        options.token = std::stoull(argv[++i]);
      } else if (flag == "--frame-port") {
        options.framePort = uint16_t(std::stoi(argv[++i]));
      } else if (flag == "--control-port") {
        options.controlPort = uint16_t(std::stoi(argv[++i]));
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
#ifndef _WIN32
  // A write to a socket the module has already closed must not kill us.
  // POSIX-only: Windows has no SIGPIPE, and a send() to a closed socket
  // reports WSAECONNRESET through the return value instead of raising
  // anything, which the socket paths already handle.
  std::signal(SIGPIPE, SIG_IGN);
#endif
  const cardscanner::util::ParentWatchdog watchdog([] { g_running = false; });

  cardscanner::http::globalInit();

  try {
    auto loaded = cardscanner::desktop::loadConfigFile(configPath);
    cardscanner::desktop::applyDataPaths(loaded.paths);
    options.products = loaded.products;

    cardscanner::log(cardscanner::LOG_LEVEL::Info, "[Server]",
                     "loading models...");
    desktop::ScannerServer server(loaded.scanner, {}, options);
    server.service().setReportTimings(timings);
    server.start();

    std::ostringstream ready;
    ready << "ready\n"
          << "  frames   127.0.0.1:" << options.framePort << "  token "
          << options.token << "\n"
          << "  control  ws://127.0.0.1:" << options.controlPort;
    cardscanner::log(cardscanner::LOG_LEVEL::Info, "[Server]", ready.str());

    if (!replayDir.empty()) {
      replayDirectory(replayDir, server);
    } else {
      while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }

    if (replayDir.empty()) {
      // Zero-zero is ambiguous: a module that never connected and one whose
      // every connection was rejected both land here. The log tells them apart.
      std::cout << "\nshutting down (" << server.framesScanned()
                << " frames scanned, " << server.framesRejected() << " unusable)\n";
    } else {
      // Replayed frames bypass the socket counters; printing them here would
      // always read zero-zero.
      std::cout << "\nshutting down\n";
    }

    // If stop() ever stalls once OBS is gone, the orphan would hold the ports
    // until someone killed it by hand.
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      cardscanner::log(cardscanner::LOG_LEVEL::Error, "[Server]",
                       "shutdown stalled; exiting");
      std::_Exit(1);
    }).detach();
    server.stop();
    cardscanner::http::globalCleanup();
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    cardscanner::http::globalCleanup();
    return 1;
  }
}
