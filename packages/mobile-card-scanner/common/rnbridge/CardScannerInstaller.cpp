#include "CardScannerInstaller.h"
#include "Constants.h"
#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include "ScannerRegistry.h"
#include <utils/ImageUtils.h>
#include "benchmark/BenchmarkRunner.h"
#include "jsi/Promise.h"
#include "JSISerializer.h"
#include <Log.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

#include "../backends/executorch/ThreadUtils.h"

using namespace cardscanner::constants;

#ifdef __ANDROID__
#include <sys/resource.h>
#endif

namespace cardscanner {

namespace {

using SettleFn = std::function<void(jsi::Runtime &, Promise &)>;

/**
 * Runs `work` on a worker thread, then settles the promise on the JS thread.
 * The worker owns no jsi state - only the Promise's weak callback handles -
 * so a runtime teardown while it runs makes the settle a no-op instead of a
 * use-after-free. Any exception escaping `work` rejects the promise.
 */
void runAsync(std::shared_ptr<Promise> promise, std::function<SettleFn()> work) {
  std::thread([promise = std::move(promise), work = std::move(work)]() {
    SettleFn settle;
    try {
      settle = work();
    } catch (const std::exception &e) {
      settle = [msg = std::string(e.what())](jsi::Runtime &rt, Promise &p) {
        p.reject(rt, msg);
      };
    } catch (...) {
      settle = [](jsi::Runtime &rt, Promise &p) {
        p.reject(rt, "Unknown native error");
      };
    }
    auto invoker = promise->getCallInvoker();
    invoker->invokeAsync(
        [promise = std::move(promise),
         settle = std::move(settle)](jsi::Runtime &rt) {
          try {
            settle(rt, *promise);
          } catch (const std::exception &e) {
            // No-op if the promise already settled.
            promise->reject(rt, e.what());
          } catch (...) {
            promise->reject(rt, "Failed to deliver native result");
          }
        });
  }).detach();
}

SettleFn settleOperationResult(bool success, std::string error) {
  return [success, error = std::move(error)](jsi::Runtime &rt, Promise &p) {
    p.resolve(rt, utils::JSISerializer::serializeOperationResult(rt, success,
                                                                 error));
  };
}

struct GameDbInfo {
  std::string name;
  std::string path;
  uint64_t cardCount = 0;
  std::string creationTimestamp;
  long fileSize = 0;
};

GameDbInfo readGameDbInfo(DatabaseManager &dbManager,
                          const std::string &gameName) {
  GameDbInfo info;
  info.name = gameName;
  info.path = dbManager.getStorePath(gameName);
  if (GameStorePtr db = dbManager.getOrCreateStore(gameName)) {
    info.cardCount = db->get_card_count();
    info.creationTimestamp = db->get_metadata_value("creation_timestamp");
  }
  info.fileSize = utils::ImageUtils::getFileSize(info.path);
  if (info.fileSize < 0) {
    info.fileSize = 0;
  }
  return info;
}

} // namespace

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {

  cardscanner::DatabaseManager &dbManager =
      cardscanner::DatabaseManager::getInstance();

  auto initializeScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "initializeScanner"),
      1,
      [callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isObject()) {
          throw jsi::JSError(runtime,
                             "initializeScannerNative expects a config object");
        }

        // Parse config object on JS thread
        jsi::Object configObj = args[0].asObject(runtime);

        // Disable OpenCV threading to prevent interference with ExecutorTorch
        cv::setNumThreads(0);

        ScannerRegistry::setConfig(
            utils::JSISerializer::parseScannerConfig(runtime, configObj));

        return Promise::createPromise(
            runtime, callInvoker, [](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), []() -> SettleFn {
                ScannerRegistry::initializeModels();
                return [](jsi::Runtime &rt, Promise &p) {
                  jsi::Object result(rt);
                  result.setProperty(rt, "success", jsi::Value(true));
                  p.resolve(rt, std::move(result));
                };
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "initializeScanner",
                                   std::move(initializeScannerFunc));

  auto releaseScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "releaseScanner"), 0,
      [callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        return Promise::createPromise(
            runtime, callInvoker, [](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), []() -> SettleFn {
                const bool released = ScannerRegistry::releaseModels();
                return [released](jsi::Runtime &rt, Promise &p) {
                  p.resolve(rt, jsi::Value(released));
                };
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "releaseScanner",
                                   std::move(releaseScannerFunc));

  auto swapDatabaseFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "swapDatabase"), 2,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(
              runtime,
              "swapDatabase expects (gameName: string, sourcePath: string)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);
        std::string sourcePath = args[1].asString(runtime).utf8(runtime);

        return Promise::createPromise(
            runtime, callInvoker,
            [gameName, sourcePath,
             &dbManager](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), [gameName, sourcePath,
                                            &dbManager]() -> SettleFn {
                // The store being swapped out may be mid-search in a scan.
                std::unique_lock<std::shared_timed_mutex> exclusive(
                    ScannerRegistry::pipelineMutex());

                const auto startTime = std::chrono::steady_clock::now();
                bool success = dbManager.swapDatabaseFile(gameName, sourcePath);
                const auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startTime)
                        .count();
                log(LOG_LEVEL::Info, "[CardScanner] Swapped database for game",
                    gameName, "in", duration, "ms.");

                if (success) {
                  dbManager.scanForExistingStores();
                }

                return settleOperationResult(
                    success, success ? "" : "Failed to swap database file");
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "swapDatabase",
                                   std::move(swapDatabaseFunc));

  auto scanImageFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "scanImage"), 2,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "scanImage expects (imagePath: string, "
                             "mode?: 'single' | 'multiple')");
        }

        std::string imagePath = args[0].asString(runtime).utf8(runtime);

        // Optional per-call scan mode; empty = the configured one.
        std::string scanMode;
        if (count >= 2 && !args[1].isUndefined() && !args[1].isNull()) {
          if (!args[1].isString()) {
            throw jsi::JSError(runtime, "scanImage: mode must be a string");
          }
          scanMode = args[1].asString(runtime).utf8(runtime);
          if (scanMode != "single" && scanMode != "multiple") {
            throw jsi::JSError(runtime, "scanImage: mode must be 'single' or "
                                        "'multiple', got '" + scanMode + "'");
          }
        }

        return Promise::createPromise(
            runtime, callInvoker,
            [imagePath, scanMode,
             &dbManager](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), [imagePath, scanMode,
                                            &dbManager]() -> SettleFn {
                try {
                  ScanResult scanResult = ScannerRegistry::scanImageFile(
                      imagePath, dbManager, scanMode);
                  return [scanResult = std::move(scanResult)](jsi::Runtime &rt,
                                                              Promise &p) {
                    p.resolve(rt, utils::JSISerializer::serializeScanResult(
                                      rt, scanResult));
                  };
                } catch (const std::exception &e) {
                  throw std::runtime_error(std::string("Image scan failed: ") +
                                           e.what());
                }
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "scanImage",
                                   std::move(scanImageFunc));

  auto runBenchmarkFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime,
      jsi::PropNameID::forAscii(*jsiRuntime, "runBenchmarkFromImages"), 3,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 3 || !args[0].isObject() ||
            !args[0].asObject(runtime).isArray(runtime) || !args[1].isNumber() ||
            !args[2].isNumber()) {
          throw jsi::JSError(
              runtime,
              "runBenchmarkFromImages expects (images: {imagePath, game, "
              "cardIds}[], warmupIterations: number, benchmarkIterations: "
              "number)");
        }

        // Parsed on the JS thread.
        jsi::Array imagesArray = args[0].asObject(runtime).asArray(runtime);
        size_t imageCount = imagesArray.size(runtime);

        std::vector<BenchmarkImageInput> images;
        images.reserve(imageCount);

        auto entryError = [&runtime](size_t index, const char *issue) {
          return jsi::JSError(runtime, "runBenchmarkFromImages: image entry " +
                                           std::to_string(index) + ' ' + issue);
        };

        for (size_t i = 0; i < imageCount; i++) {
          jsi::Value entryVal = imagesArray.getValueAtIndex(runtime, i);
          if (!entryVal.isObject()) {
            throw entryError(i, "is not an object");
          }
          jsi::Object entry = entryVal.asObject(runtime);
          jsi::Value imagePathVal = entry.getProperty(runtime, "imagePath");
          if (!imagePathVal.isString()) {
            throw entryError(i, "is missing a string 'imagePath'");
          }
          jsi::Value gameVal = entry.getProperty(runtime, "game");
          if (!gameVal.isString()) {
            throw entryError(i, "is missing a string 'game'");
          }

          BenchmarkImageInput input;
          input.imagePath = imagePathVal.asString(runtime).utf8(runtime);
          input.game = gameVal.asString(runtime).utf8(runtime);

          // Omitted when the photo has no ground truth; the scan is then timed
          // but not scored.
          jsi::Value cardIdsVal = entry.getProperty(runtime, "cardIds");
          if (!cardIdsVal.isUndefined() && !cardIdsVal.isNull()) {
            if (!cardIdsVal.isObject() ||
                !cardIdsVal.asObject(runtime).isArray(runtime)) {
              throw entryError(i, "has a non-array 'cardIds'");
            }
            jsi::Array cardIds = cardIdsVal.asObject(runtime).asArray(runtime);
            size_t idCount = cardIds.size(runtime);
            input.cardIds.reserve(idCount);
            for (size_t j = 0; j < idCount; j++) {
              jsi::Value idVal = cardIds.getValueAtIndex(runtime, j);
              if (!idVal.isString()) {
                throw entryError(i, "has a non-string id in 'cardIds'");
              }
              input.cardIds.push_back(idVal.asString(runtime).utf8(runtime));
            }
          }

          images.push_back(std::move(input));
        }

        double warmupRaw = args[1].asNumber();
        double benchmarkRaw = args[2].asNumber();
        if (!(warmupRaw >= 0) || !(benchmarkRaw >= 0)) {
          throw jsi::JSError(runtime,
                             "runBenchmarkFromImages: warmupIterations and "
                             "benchmarkIterations must not be negative");
        }

        int warmupIterations = static_cast<int>(warmupRaw);
        int benchmarkIterations = static_cast<int>(benchmarkRaw);
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, images = std::move(images), warmupIterations,
             benchmarkIterations](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), [&dbManager, images = std::move(images),
                                            warmupIterations,
                                            benchmarkIterations]() -> SettleFn {
                ScannerRegistry::beginBenchmark();
                struct BenchmarkRunningGuard {
                  ~BenchmarkRunningGuard() { ScannerRegistry::endBenchmark(); }
                } benchmarkRunningGuard;

                // New scans are already turned away by the flag above; this
                // waits out the ones that started before it was set.
                std::unique_lock<std::shared_timed_mutex> exclusive(
                    ScannerRegistry::pipelineMutex());

                auto ctx = ScannerRegistry::getScannerContext();

                if (!ctx.yoloModel || !ctx.embeddingModel) {
                  throw std::runtime_error(
                      "Models not initialized. Call initializeScanner() "
                      "first.");
                }

                auto runResult = benchmark::BenchmarkRunner::run(
                    images, ctx.config, warmupIterations, benchmarkIterations,
                    dbManager, ctx.yoloModel.get(), ctx.embeddingModel.get(),
                    ctx.setSymbolYoloModel.get(), ctx.setSymbolEmbedder.get(),
                    ctx.fabColorClassifier.get(), &ctx.gameEmbeddingModels);

                return [runResult =
                            std::move(runResult)](jsi::Runtime &rt, Promise &p) {
                  jsi::Object result(rt);
                  result.setProperty(rt, "success", jsi::Value(true));
                  result.setProperty(
                      rt, "recordCount",
                      jsi::Value(static_cast<double>(runResult.recordCount)));
                  result.setProperty(
                      rt, "recordsJson",
                      jsi::String::createFromUtf8(rt, runResult.json));
                  p.resolve(rt, std::move(result));
                };
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "runBenchmarkFromImages",
                                   std::move(runBenchmarkFunc));

  auto listDatabasesFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "listDatabases"), 0,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), [&dbManager]() -> SettleFn {
                dbManager.scanForExistingStores();

                std::vector<GameDbInfo> infos;
                for (const auto &gameName : dbManager.getKnownGames()) {
                  try {
                    infos.push_back(readGameDbInfo(dbManager, gameName));
                  } catch (...) {
                    // Ignore errors for individual games
                  }
                }

                return [infos = std::move(infos)](jsi::Runtime &rt,
                                                  Promise &p) {
                  jsi::Array result(rt, infos.size());
                  for (size_t i = 0; i < infos.size(); i++) {
                    const auto &info = infos[i];
                    result.setValueAtIndex(
                        rt, i,
                        utils::JSISerializer::serializeDatabaseInfo(
                            rt, info.name, info.path, info.cardCount,
                            info.creationTimestamp, info.fileSize));
                  }
                  p.resolve(rt, std::move(result));
                };
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "listDatabases",
                                   std::move(listDatabasesFunc));

  auto getDatabaseInfoFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "getDatabaseInfo"), 1,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "getDatabaseInfo expects one string argument "
                             "(gameName)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);

        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, gameName](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), [&dbManager, gameName]() -> SettleFn {
                try {
                  if (!std::filesystem::exists(dbManager.getStorePath(gameName))) {
                    throw std::runtime_error(
                        std::string("Database not found for game: ") + gameName);
                  }

                  GameDbInfo info = readGameDbInfo(dbManager, gameName);
                  return [info = std::move(info)](jsi::Runtime &rt, Promise &p) {
                    p.resolve(rt, utils::JSISerializer::serializeDatabaseInfo(
                                      rt, info.name, info.path, info.cardCount,
                                      info.creationTimestamp, info.fileSize));
                  };
                } catch (const std::exception &e) {
                  throw std::runtime_error(
                      std::string("Failed to get database info for ") +
                      gameName + ": " + e.what());
                }
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "getDatabaseInfo",
                                   std::move(getDatabaseInfoFunc));

  auto doesCardIdExistFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "doesCardIdExist"), 2,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(runtime,
                             "doesCardIdExist expects two string arguments "
                             "(gameName, cardId)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);
        std::string cardId = args[1].asString(runtime).utf8(runtime);

        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, gameName, cardId](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), [&dbManager, gameName,
                                            cardId]() -> SettleFn {
                try {
                  bool exists = dbManager.cardIdExists(gameName, cardId);
                  return [exists](jsi::Runtime &rt, Promise &p) {
                    p.resolve(rt, jsi::Value(exists));
                  };
                } catch (const std::exception &e) {
                  throw std::runtime_error(
                      std::string("Failed to look up card_id for ") + gameName +
                      ": " + e.what());
                }
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "doesCardIdExist",
                                   std::move(doesCardIdExistFunc));

  auto deleteDatabaseFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "deleteDatabase"), 1,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "deleteDatabase expects one string argument "
                             "(gameName)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);

        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, gameName](std::shared_ptr<Promise> promise) {
              runAsync(std::move(promise), [&dbManager, gameName]() -> SettleFn {
                try {
                  // The store being deleted may be mid-search in a scan.
                  std::unique_lock<std::shared_timed_mutex> exclusive(
                      ScannerRegistry::pipelineMutex());

                  bool success = dbManager.deleteDatabaseDirectory(gameName);
                  return settleOperationResult(
                      success, success ? ""
                                       : std::string(
                                             "Failed to delete database for ") +
                                             gameName);
                } catch (const std::exception &e) {
                  return settleOperationResult(false, e.what());
                }
              });
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "deleteDatabase",
                                   std::move(deleteDatabaseFunc));

  // Frame scanning is no longer installed. VisionCamera v5 hands Frames
  // over as Nitro HybridObjects and the plugin is the autolinked
  // HybridCardScannerPlugin rather than a scanFramePlugin global.

  // Once per process: resizing the pool on a reload could pull it out from
  // under an inference still running from before the reload.
  static std::once_flag threadPoolOnce;
  std::call_once(threadPoolOnce,
                 [] { threads::utils::unsafeSetupThreadPool(); });
}

} // namespace cardscanner
