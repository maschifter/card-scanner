#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cardscanner {

/**
 * @brief A config document in a neutral shape: whatever a JSON value holds.
 *
 * Each host converts its own parsed config (nlohmann::json on desktop,
 * jsi::Value on mobile) into this, and core parses it once in
 * parseScannerConfig. The converters know nothing about the fields, so adding
 * a field never touches them.
 */
struct ConfigValue {
  using Array = std::vector<ConfigValue>;
  /// Members in document order.
  using Object = std::vector<std::pair<std::string, ConfigValue>>;

  std::variant<std::monostate, bool, double, std::string, Array, Object> value;

  bool isNull() const { return std::holds_alternative<std::monostate>(value); }
  bool isObject() const { return std::holds_alternative<Object>(value); }

  /// The member named `key`; a null value when absent or not an object.
  const ConfigValue &operator[](std::string_view key) const;

  /// The JSON name of the held type, for error messages.
  const char *typeName() const;
};

inline const ConfigValue &ConfigValue::operator[](std::string_view key) const {
  static const ConfigValue null;
  if (const auto *members = std::get_if<Object>(&value)) {
    for (const auto &[name, member] : *members) {
      if (name == key) {
        return member;
      }
    }
  }
  return null;
}

inline const char *ConfigValue::typeName() const {
  constexpr const char *names[] = {"null",   "boolean", "number",
                                   "string", "array",   "object"};
  return names[value.index()];
}

} // namespace cardscanner
