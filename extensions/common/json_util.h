/* Copyright 2020 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "extensions/common/nlohmann_json.hpp"

/**
 * Utilities for working with JSON without exceptions.
 */
namespace Wasm {
namespace Common {

using JsonObject = ::nlohmann::json;
using JsonParserException = ::nlohmann::detail::exception;
using JsonParserOutOfRangeException = ::nlohmann::detail::out_of_range;
using JsonParserTypeErrorException = ::nlohmann::detail::type_error;

// Parse JSON. Returns the discarded value if fails.
JsonObject JsonParse(absl::string_view str,
                     const bool allow_exceptions = false);

template <typename T>
absl::optional<T> JsonValueAs(const JsonObject&, const bool) {
  static_assert(true, "Unsupported Type");
}

template <>
absl::optional<absl::string_view> JsonValueAs<absl::string_view>(
    const JsonObject& j, const bool allow_exception);

template <>
absl::optional<std::string> JsonValueAs<std::string>(
    const JsonObject& j, const bool allow_exception);

template <>
absl::optional<int64_t> JsonValueAs<int64_t>(const JsonObject& j,
                                             const bool allow_exception);

template <>
absl::optional<uint64_t> JsonValueAs<uint64_t>(const JsonObject& j,
                                               const bool allow_exception);

template <>
absl::optional<bool> JsonValueAs<bool>(const JsonObject& j,
                                       const bool allow_exception);

template <typename T>
absl::optional<T> JsonGetField(const JsonObject& j, absl::string_view field,
                               const bool allow_exception = false) {
  auto it = j.find(field);
  if (it == j.end()) {
    if (allow_exception) {
      throw JsonParserOutOfRangeException::create(
          403, "Key " + std::string(field) + " is not found");
    }
    return absl::nullopt;
  }
  return JsonValueAs<T>(it.value(), allow_exception);
}

// Iterate over an optional array field.
// Returns false if set and not an array, or any of the visitor calls returns
// false.
template <typename T>
bool JsonArrayIterate(const JsonObject&, absl::string_view,
                      const std::function<bool(const T& elt)>&) {
  static_assert(true, "Unsupported type");
}

template <>
bool JsonArrayIterate(
    const JsonObject& j, absl::string_view field,
    const std::function<bool(const JsonObject& elt)>& visitor);

template <>
bool JsonArrayIterate<std::string>(
    const JsonObject& j, absl::string_view field,
    const std::function<bool(const std::string& elt)>& visitor);

// Iterate over an optional object field key set.
// Returns false if set and not an object, or any of the visitor calls returns
// false.
bool JsonObjectIterate(const JsonObject& j, absl::string_view field,
                       const std::function<bool(std::string key)>& visitor);

}  // namespace Common
}  // namespace Wasm
