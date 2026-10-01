#include "JSISerializer.h"
#include "NitroSerializer.h"
#include <Constants.h>
#include <string>

namespace cardscanner {
namespace utils {

using namespace cardscanner::constants;

jsi::Object JSISerializer::serializeScanResult(jsi::Runtime &runtime,
                                               const ScanResult &result) {
  // Reuse the dto. Nitro struct mapping and the nitrogen-generated
  // struct -> JSI conversion so both paths share one serialization.
  auto detection = NitroSerializer::serializeScanResult(result);
  return margelo::nitro::JSIConverter<
             margelo::nitro::cardscanner::NitroDetection>::toJSI(runtime,
                                                                 detection)
      .asObject(runtime);
}

namespace {

/// The value in core's neutral shape. Knows no config keys; functions,
/// symbols and undefined become null.
ConfigValue toConfigValue(jsi::Runtime &runtime, const jsi::Value &value) {
  if (value.isBool()) {
    return {value.getBool()};
  }
  if (value.isNumber()) {
    return {value.getNumber()};
  }
  if (value.isString()) {
    return {value.getString(runtime).utf8(runtime)};
  }
  if (!value.isObject()) {
    return {};
  }
  const jsi::Object object = value.getObject(runtime);
  if (object.isArray(runtime)) {
    const jsi::Array array = object.getArray(runtime);
    const size_t count = array.size(runtime);
    ConfigValue::Array items;
    items.reserve(count);
    for (size_t i = 0; i < count; i++) {
      items.push_back(toConfigValue(runtime, array.getValueAtIndex(runtime, i)));
    }
    return {std::move(items)};
  }
  if (object.isFunction(runtime)) {
    return {};
  }
  const jsi::Array names = object.getPropertyNames(runtime);
  const size_t count = names.size(runtime);
  ConfigValue::Object members;
  members.reserve(count);
  for (size_t i = 0; i < count; i++) {
    const jsi::String name = names.getValueAtIndex(runtime, i).getString(runtime);
    members.emplace_back(name.utf8(runtime),
                         toConfigValue(runtime, object.getProperty(runtime, name)));
  }
  return {std::move(members)};
}

} // namespace

ScannerConfig
JSISerializer::parseScannerConfig(jsi::Runtime &runtime,
                                  const jsi::Object &configObj) {
  auto parsed = cardscanner::parseScannerConfig(
      toConfigValue(runtime, jsi::Value(runtime, configObj)));
  return std::move(parsed.config);
}

jsi::Object JSISerializer::serializeDatabaseInfo(
    jsi::Runtime &runtime, const std::string &gameName, const std::string &path,
    uint64_t cardCount, const std::string &creationTimestamp, long fileSize) {
  jsi::Object dbInfo(runtime);
  dbInfo.setProperty(runtime, "gameName",
                     jsi::String::createFromUtf8(runtime, gameName));
  dbInfo.setProperty(runtime, "path",
                     jsi::String::createFromUtf8(runtime, path));
  dbInfo.setProperty(runtime, "cardCount",
                     jsi::Value(static_cast<double>(cardCount)));
  dbInfo.setProperty(runtime, "creationTimestamp",
                     jsi::String::createFromUtf8(runtime, creationTimestamp));
  if (fileSize > 0) {
    dbInfo.setProperty(runtime, "fileSize",
                       jsi::Value(static_cast<double>(fileSize)));
  }
  return dbInfo;
}

jsi::Object JSISerializer::serializeOperationResult(jsi::Runtime &runtime,
                                                    bool success,
                                                    const std::string &error) {
  jsi::Object result(runtime);
  result.setProperty(runtime, "success", jsi::Value(success));
  if (!success && !error.empty()) {
    result.setProperty(runtime, "error",
                       jsi::String::createFromUtf8(runtime, error));
  }
  return result;
}

} // namespace utils
} // namespace cardscanner
