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
#include <iostream>
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>

#include "../backends/executorch/ThreadUtils.h"

using namespace cardscanner::constants;

#ifdef __ANDROID__
#include <sys/resource.h>
#endif

namespace cardscanner {

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {

  cardscanner::DatabaseManager &dbManager =
      cardscanner::DatabaseManager::getInstance();

  // Create the 'initializeScannerNative' host function (returns Promise)
  auto initializeScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "initializeScanner"),
      1,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
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

        // Return a Promise that runs initialization on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager](std::shared_ptr<Promise> promise) {
              // Run initialization on background thread
              std::thread([&dbManager, promise]() {
                try {
                  ScannerRegistry::initializeModels();

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync([promise]() {
                    jsi::Object result(promise->getRuntime());
                    result.setProperty(promise->getRuntime(), "success",
                                       jsi::Value(true));
                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  // Reject promise on JS thread
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        jsi::Object result(promise->getRuntime());
                        result.setProperty(promise->getRuntime(), "success",
                                           jsi::Value(false));
                        result.setProperty(
                            promise->getRuntime(), "error",
                            jsi::String::createFromUtf8(promise->getRuntime(),
                                                        errorMsg));
                        promise->resolve(std::move(result));
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "initializeScanner",
                                   std::move(initializeScannerFunc));

  // Create the 'releaseScanner' host function
  auto releaseScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "releaseScanner"), 0,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        ScannerRegistry::releaseModels();
        return jsi::Value(true);
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "releaseScanner",
                                   std::move(releaseScannerFunc));

  // Create the 'swapDatabaseNative' host function (returns Promise)
  auto swapDatabaseFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "swapDatabase"), 2,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(
              runtime,
              "swapDatabase expects (sourcePath: string, gameName: string)");
        }

        std::string sourcePath = args[0].asString(runtime).utf8(runtime);
        std::string gameName = args[1].asString(runtime).utf8(runtime);

        // Return a Promise that runs database swap on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [sourcePath, gameName,
             &dbManager](std::shared_ptr<Promise> promise) {
              // Run database swap on background thread
              std::thread([sourcePath, gameName, &dbManager, promise]() {
                try {
                  // The store being swapped out may be mid-search in a scan.
                  std::unique_lock<std::shared_timed_mutex> exclusive(
                      ScannerRegistry::pipelineMutex());

                  std::chrono::steady_clock::time_point startTime =
                      std::chrono::steady_clock::now();
                  bool success =
                      dbManager.swapDatabaseFile(gameName, sourcePath);
                  std::chrono::steady_clock::time_point endTime =
                      std::chrono::steady_clock::now();
                  auto duration =
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          endTime - startTime)
                          .count();
                  std::cout << "[CardScanner] Swapped database for game '"
                            << gameName << "' in " << duration << " ms."
                            << std::endl;

                  if (success) {
                    dbManager.scanForExistingStores();
                  }

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync([promise, success]() {
                    jsi::Object result(promise->getRuntime());
                    result.setProperty(promise->getRuntime(), "success",
                                       jsi::Value(success));
                    if (!success) {
                      result.setProperty(promise->getRuntime(), "error",
                                         jsi::String::createFromUtf8(
                                             promise->getRuntime(),
                                             "Failed to swap database file"));
                    }
                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  // Reject promise on JS thread
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        jsi::Object result(promise->getRuntime());
                        result.setProperty(promise->getRuntime(), "success",
                                           jsi::Value(false));
                        result.setProperty(
                            promise->getRuntime(), "error",
                            jsi::String::createFromUtf8(promise->getRuntime(),
                                                        errorMsg));
                        promise->resolve(std::move(result));
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "swapDatabase",
                                   std::move(swapDatabaseFunc));

  // Create the 'scanImage' host function
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
              std::thread([imagePath, scanMode, &dbManager,
                           promise]() {
                auto invoker = promise->getCallInvoker();
                try {
                  ScanResult scanResult = ScannerRegistry::scanImageFile(
                      imagePath, dbManager, scanMode);

                  invoker->invokeAsync(
                      [promise, scanResult = std::move(scanResult)]() {
                        try {
                          auto result =
                              utils::JSISerializer::serializeScanResult(
                                  promise->getRuntime(), scanResult);
                          promise->resolve(std::move(result));
                        } catch (const std::exception &e) {
                          promise->reject(std::string("Image scan failed: ") +
                                          e.what());
                        } catch (...) {
                          promise->reject("Image scan failed: unknown error");
                        }
                      });
                } catch (const std::exception &e) {
                  invoker->invokeAsync([promise,
                                        errorMsg = std::string(e.what())]() {
                    promise->reject("Image scan failed: " + errorMsg);
                  });
                } catch (...) {
                  invoker->invokeAsync([promise]() {
                    promise->reject("Image scan failed: unknown error");
                  });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "scanImage",
                                   std::move(scanImageFunc));

  // Runs the pipeline on a detached worker thread.
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
              std::thread([&dbManager, images, warmupIterations,
                          benchmarkIterations, promise]() {
                try {
                  ScannerRegistry::beginBenchmark();
                  struct BenchmarkRunningGuard {
                    ~BenchmarkRunningGuard() {
                      ScannerRegistry::endBenchmark();
                    }
                  } benchmarkRunningGuard;

                  // New scans are already turned away by the flag above; this
                  // waits out the ones that started before it was set.
                  std::unique_lock<std::shared_timed_mutex> exclusive(
                      ScannerRegistry::pipelineMutex());

                  auto ctx = ScannerRegistry::getScannerContext();

                  if (!ctx.yoloModel || !ctx.embeddingModel) {
                    promise->getCallInvoker()->invokeAsync([promise]() {
                      promise->reject("Models not initialized. Call "
                                      "initializeScanner() first.");
                    });
                    return;
                  }

                  auto runResult = benchmark::BenchmarkRunner::run(
                      images, ctx.config, warmupIterations,
                      benchmarkIterations, dbManager, ctx.yoloModel.get(),
                      ctx.embeddingModel.get(), ctx.setSymbolYoloModel.get(),
                      ctx.setSymbolEmbedder.get(), ctx.fabColorClassifier.get(),
                      &ctx.gameEmbeddingModels);

                  promise->getCallInvoker()->invokeAsync(
                      [promise, runResult = std::move(runResult)]() {
                        auto &rt = promise->getRuntime();
                        jsi::Object result(rt);
                        result.setProperty(rt, "success", jsi::Value(true));
                        result.setProperty(
                            rt, "recordCount",
                            jsi::Value(
                                static_cast<double>(runResult.recordCount)));

                        result.setProperty(
                            rt, "recordsJson",
                            jsi::String::createFromUtf8(rt, runResult.json));
                        promise->resolve(std::move(result));
                      });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        promise->reject(std::string("Benchmark run failed: ") +
                                        errorMsg);
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "runBenchmarkFromImages",
                                   std::move(runBenchmarkFunc));

  auto listDatabasesFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "listDatabases"), 0,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        // Return a Promise that scans for games on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager](std::shared_ptr<Promise> promise) {
              std::thread([&dbManager, promise]() {
                try {
                  dbManager.scanForExistingStores();
                  std::set<std::string> games = dbManager.getKnownGames();

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync([promise, games,
                                                          &dbManager]() {
                    jsi::Array result(promise->getRuntime(), games.size());
                    size_t i = 0;

                    for (const auto &gameName : games) {
                      std::string dirPath = dbManager.getStorePath(gameName);

                      // Get metadata
                      uint64_t cardCount = 0;
                      std::string creationTimestamp = "";
                      long fileSize = 0;

                      try {
                        GameStorePtr db = dbManager.getOrCreateStore(gameName);
                        if (db) {
                          cardCount = db->get_card_count();
                          creationTimestamp =
                              db->get_metadata_value("creation_timestamp");
                        }

                        // Get file size
                        fileSize = utils::ImageUtils::getFileSize(dirPath);
                        if (fileSize < 0) {
                          fileSize = 0;
                        }
                      } catch (...) {
                        // Ignore errors for individual games
                      }

                      // Use JSISerializer for consistent serialization
                      jsi::Object gameObj =
                          utils::JSISerializer::serializeDatabaseInfo(
                              promise->getRuntime(), gameName, dirPath,
                              cardCount, creationTimestamp, fileSize);

                      result.setValueAtIndex(promise->getRuntime(), i++,
                                             std::move(gameObj));
                    }

                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        promise->reject(errorMsg);
                      });
                }
              }).detach();
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

        // Return a Promise that retrieves database info on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, gameName](std::shared_ptr<Promise> promise) {
              std::thread([&dbManager, gameName, promise]() {
                try {
                  std::string dirPath = dbManager.getStorePath(gameName);

                  // Check if database exists
                  if (!std::filesystem::exists(dirPath)) {
                    promise->getCallInvoker()->invokeAsync(
                        [promise, gameName]() {
                          promise->reject(
                              std::string("Database not found for game: ") +
                              gameName);
                        });
                    return;
                  }

                  // Get metadata
                  uint64_t cardCount = 0;
                  std::string creationTimestamp = "";
                  long fileSize = 0;

                  GameStorePtr db = dbManager.getOrCreateStore(gameName);
                  if (db) {
                    cardCount = db->get_card_count();
                    creationTimestamp =
                        db->get_metadata_value("creation_timestamp");
                  }

                  // Get file size
                  fileSize = utils::ImageUtils::getFileSize(dirPath);
                  if (fileSize < 0) {
                    fileSize = 0;
                  }

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync(
                      [promise, gameName, dirPath, cardCount, creationTimestamp,
                       fileSize]() {
                        // Use JSISerializer for consistent serialization
                        jsi::Object gameObj =
                            utils::JSISerializer::serializeDatabaseInfo(
                                promise->getRuntime(), gameName, dirPath,
                                cardCount, creationTimestamp, fileSize);

                        promise->resolve(std::move(gameObj));
                      });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, gameName, errorMsg = std::string(e.what())]() {
                        promise->reject(
                            std::string("Failed to get database info for ") +
                            gameName + ": " + errorMsg);
                      });
                }
              }).detach();
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
              std::thread([&dbManager, gameName, cardId, promise]() {
                try {
                  bool exists = dbManager.cardIdExists(gameName, cardId);
                  promise->getCallInvoker()->invokeAsync([promise, exists]() {
                    promise->resolve(jsi::Value(exists));
                  });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, gameName, errorMsg = std::string(e.what())]() {
                        promise->reject(
                            std::string("Failed to look up card_id for ") +
                            gameName + ": " + errorMsg);
                      });
                }
              }).detach();
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

        // Return a Promise that deletes database on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, gameName](std::shared_ptr<Promise> promise) {
              std::thread([&dbManager, gameName, promise]() {
                try {
                  // The store being deleted may be mid-search in a scan.
                  std::unique_lock<std::shared_timed_mutex> exclusive(
                      ScannerRegistry::pipelineMutex());

                  bool success = dbManager.deleteDatabaseDirectory(gameName);

                  promise->getCallInvoker()->invokeAsync([promise, success,
                                                          gameName]() {
                    // Use JSISerializer for consistent serialization
                    std::string error =
                        success
                            ? ""
                            : std::string("Failed to delete database for ") +
                                  gameName;
                    jsi::Object result =
                        utils::JSISerializer::serializeOperationResult(
                            promise->getRuntime(), success, error);

                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        // Use JSISerializer for consistent serialization
                        jsi::Object result =
                            utils::JSISerializer::serializeOperationResult(
                                promise->getRuntime(), false, errorMsg);
                        promise->resolve(std::move(result));
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "deleteDatabase",
                                   std::move(deleteDatabaseFunc));

  // Frame scanning is no longer installed. VisionCamera v5 hands Frames
  // over as Nitro HybridObjects and the plugin is the autolinked
  // HybridCardScannerPlugin rather than a scanFramePlugin global.

  threads::utils::unsafeSetupThreadPool();
}

} // namespace cardscanner
