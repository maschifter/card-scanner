#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <jsi/jsi.h>
#include <rncardscanner/jsi/OwningArrayBuffer.h>

namespace rncardscanner::jsiconversion {

using namespace facebook;

// Conversion from jsi to C++ types --------------------------------------------

template <typename T> T getValue(const jsi::Value &val, jsi::Runtime &runtime);

template <>
inline bool getValue<bool>(const jsi::Value &val, jsi::Runtime &runtime) {
  return val.asBool();
}

template <>
inline std::string getValue<std::string>(const jsi::Value &val,
                                         jsi::Runtime &runtime) {
  return val.getString(runtime).utf8(runtime);
}

template <>
inline std::shared_ptr<jsi::Function>
getValue<std::shared_ptr<jsi::Function>>(const jsi::Value &val,
                                         jsi::Runtime &runtime) {
  return std::make_shared<jsi::Function>(
      val.asObject(runtime).asFunction(runtime));
}

template <>
inline std::vector<int32_t>
getValue<std::vector<int32_t>>(const jsi::Value &val, jsi::Runtime &runtime) {
  jsi::Array array = val.asObject(runtime).asArray(runtime);
  size_t length = array.size(runtime);
  std::vector<int32_t> result;
  result.reserve(length);

  for (size_t i = 0; i < length; ++i) {
    jsi::Value element = array.getValueAtIndex(runtime, i);
    result.push_back(getValue<int32_t>(element, runtime));
  }
  return result;
}

template <>
inline std::vector<std::string>
getValue<std::vector<std::string>>(const jsi::Value &val,
                                   jsi::Runtime &runtime) {
  jsi::Array array = val.asObject(runtime).asArray(runtime);
  size_t length = array.size(runtime);
  std::vector<std::string> result;
  result.reserve(length);

  for (size_t i = 0; i < length; ++i) {
    jsi::Value element = array.getValueAtIndex(runtime, i);
    result.push_back(getValue<std::string>(element, runtime));
  }
  return result;
}

// C++ set from JS array. Set with heterogenerous look-up (adding std::less<>
// enables querying with std::string_view).
template <>
inline std::set<std::string, std::less<>>
getValue<std::set<std::string, std::less<>>>(const jsi::Value &val,
                                             jsi::Runtime &runtime) {

  jsi::Array array = val.asObject(runtime).asArray(runtime);
  size_t length = array.size(runtime);
  std::set<std::string, std::less<>> result;

  for (size_t i = 0; i < length; ++i) {
    jsi::Value element = array.getValueAtIndex(runtime, i);
    result.insert(getValue<std::string>(element, runtime));
  }
  return result;
}

// Helper function to convert typed arrays to std::span
template <typename T>
inline std::span<T> getTypedArrayAsSpan(const jsi::Value &val,
                                        jsi::Runtime &runtime) {
  jsi::Object obj = val.asObject(runtime);

  const bool isValidTypedArray = obj.hasProperty(runtime, "buffer") &&
                                 obj.hasProperty(runtime, "byteOffset") &&
                                 obj.hasProperty(runtime, "byteLength") &&
                                 obj.hasProperty(runtime, "length");
  if (!isValidTypedArray) {
    throw jsi::JSError(runtime, "Value must be a TypedArray");
  }

  // Get the underlying ArrayBuffer
  jsi::Value bufferValue = obj.getProperty(runtime, "buffer");
  if (!bufferValue.isObject() ||
      !bufferValue.asObject(runtime).isArrayBuffer(runtime)) {
    throw jsi::JSError(runtime,
                       "TypedArray buffer property must be an ArrayBuffer");
  }

  jsi::ArrayBuffer arrayBuffer =
      bufferValue.asObject(runtime).getArrayBuffer(runtime);
  size_t byteOffset =
      getValue<size_t>(obj.getProperty(runtime, "byteOffset"), runtime);
  size_t length = getValue<size_t>(obj.getProperty(runtime, "length"), runtime);

  T *dataPtr = reinterpret_cast<T *>(
      static_cast<uint8_t *>(arrayBuffer.data(runtime)) + byteOffset);

  return {dataPtr, length};
}

template <typename T>
inline std::vector<T> getArrayAsVector(const jsi::Value &val,
                                       jsi::Runtime &runtime) {
  jsi::Array array = val.asObject(runtime).asArray(runtime);
  const size_t length = array.size(runtime);
  std::vector<T> result;
  result.reserve(length);

  for (size_t i = 0; i < length; ++i) {
    const jsi::Value element = array.getValueAtIndex(runtime, i);
    result.push_back(getValue<T>(element, runtime));
  }
  return result;
}

// Template specializations for std::vector<T> types
template <>
inline std::vector<float> getValue<std::vector<float>>(const jsi::Value &val,
                                                       jsi::Runtime &runtime) {
  return getArrayAsVector<float>(val, runtime);
}

template <>
inline std::vector<int64_t>
getValue<std::vector<int64_t>>(const jsi::Value &val, jsi::Runtime &runtime) {
  return getArrayAsVector<int64_t>(val, runtime);
}

// Template specializations for std::span<T> types
template <>
inline std::span<float> getValue<std::span<float>>(const jsi::Value &val,
                                                   jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<float>(val, runtime);
}

template <>
inline std::span<double> getValue<std::span<double>>(const jsi::Value &val,
                                                     jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<double>(val, runtime);
}

template <>
inline std::span<int32_t> getValue<std::span<int32_t>>(const jsi::Value &val,
                                                       jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<int32_t>(val, runtime);
}

template <>
inline std::span<uint32_t>
getValue<std::span<uint32_t>>(const jsi::Value &val, jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<uint32_t>(val, runtime);
}

template <>
inline std::span<int16_t> getValue<std::span<int16_t>>(const jsi::Value &val,
                                                       jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<int16_t>(val, runtime);
}

template <>
inline std::span<uint16_t>
getValue<std::span<uint16_t>>(const jsi::Value &val, jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<uint16_t>(val, runtime);
}

template <>
inline std::span<int8_t> getValue<std::span<int8_t>>(const jsi::Value &val,
                                                     jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<int8_t>(val, runtime);
}

template <>
inline std::span<uint8_t> getValue<std::span<uint8_t>>(const jsi::Value &val,
                                                       jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<uint8_t>(val, runtime);
}

template <>
inline std::span<int64_t> getValue<std::span<int64_t>>(const jsi::Value &val,
                                                       jsi::Runtime &runtime) {
  return getTypedArrayAsSpan<int64_t>(val, runtime);
}

// Conversion from C++ types to jsi --------------------------------------------

// Implementation functions might return any type, but in a promise we can only
// return jsi::Value or jsi::Object. For each type being returned
// we add a function here.

inline jsi::Value getJsiValue(std::shared_ptr<jsi::Object> valuePtr,
                              jsi::Runtime &runtime) {
  return std::move(*valuePtr);
}

inline jsi::Value getJsiValue(const std::vector<int32_t> &vec,
                              jsi::Runtime &runtime) {
  jsi::Array array(runtime, vec.size());
  for (size_t i = 0; i < vec.size(); i++) {
    array.setValueAtIndex(runtime, i, jsi::Value(static_cast<int>(vec[i])));
  }
  return jsi::Value(runtime, array);
}

inline jsi::Value getJsiValue(int val, jsi::Runtime &runtime) {
  return jsi::Value(runtime, val);
}

inline jsi::Value getJsiValue(const std::shared_ptr<OwningArrayBuffer> &buf,
                              jsi::Runtime &runtime) {
  jsi::ArrayBuffer arrayBuffer(runtime, buf);
  return jsi::Value(runtime, arrayBuffer);
}

inline jsi::Value
getJsiValue(const std::vector<std::shared_ptr<OwningArrayBuffer>> &vec,
            jsi::Runtime &runtime) {
  jsi::Array array(runtime, vec.size());
  for (size_t i = 0; i < vec.size(); i++) {
    jsi::ArrayBuffer arrayBuffer(runtime, vec[i]);
    array.setValueAtIndex(runtime, i, jsi::Value(runtime, arrayBuffer));
  }
  return jsi::Value(runtime, array);
}

inline jsi::Value getJsiValue(const std::string &str, jsi::Runtime &runtime) {
  return jsi::String::createFromAscii(runtime, str);
}

inline jsi::Value getJsiValue(const std::vector<float> &vec,
                              jsi::Runtime &runtime) {
  jsi::Array array(runtime, vec.size());
  for (size_t i = 0; i < vec.size(); i++) {
    array.setValueAtIndex(runtime, i, jsi::Value(static_cast<float>(vec[i])));
  }
  return jsi::Value(runtime, array);
}

} // namespace rncardscanner::jsiconversion
