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

#include "extensions/authn/filter_context.h"

#include "absl/strings/str_cat.h"
#include "src/envoy/utils/filter_names.h"

using istio::authn::Payload;
using istio::authn::Result;

// WASM_PROLOG
#ifndef NULL_PLUGIN

#include "proxy_wasm_intrinsics.h"

#else  // NULL_PLUGIN

#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace AuthN {

#endif  // NULL_PLUGIN

using Envoy::Extensions::HttpFilters::HttpFilterNames;
using Envoy::Utils::IstioFilterName;
using google::protobuf::util::MessageToJsonString;

void FilterContext::setPeerResult(const Payload* payload) {
  if (payload != nullptr) {
    switch (payload->payload_case()) {
      case Payload::kX509:
        // logDebug(
        //     absl::StrCat("Set peer from X509: {}", payload->x509().user()));
        result_.set_peer_user(payload->x509().user());
        break;
      case Payload::kJwt:
        // logDebug(absl::StrCat("Set peer from JWT: {}",
        // payload->jwt().user()));
        result_.set_peer_user(payload->jwt().user());
        break;
      default:
        // logDebug("Payload has not peer authentication data");
        break;
    }
  }
}
void FilterContext::setOriginResult(const Payload* payload) {
  // Authentication pass, look at the return payload and store to the context
  // output. Set filter to continueDecoding when done.
  // At the moment, only JWT can be used for origin authentication, so
  // it's ok just to check jwt payload.
  if (payload != nullptr && payload->has_jwt()) {
    *result_.mutable_origin() = payload->jwt();
  }
}

bool FilterContext::getJwtPayload(const std::string& issuer,
                                  std::string* payload) const {
  // Prefer to use the jwt payload from Envoy jwt filter over the Istio jwt
  // filter's one.
  return getJwtPayloadFromEnvoyJwtFilter(issuer, payload) ||
         getJwtPayloadFromIstioJwtFilter(issuer, payload);
}

bool FilterContext::getJwtPayloadFromEnvoyJwtFilter(
    const std::string& issuer, std::string* payload) const {
  // Try getting the Jwt payload from Envoy jwt_authn filter.
  auto filter_it =
      dynamic_metadata_.filter_metadata().find(HttpFilterNames::get().JwtAuthn);
  if (filter_it == dynamic_metadata_.filter_metadata().end()) {
    return false;
  }

  const auto& data_struct = filter_it->second;

  const auto entry_it = data_struct.fields().find(issuer);
  if (entry_it == data_struct.fields().end()) {
    return false;
  }

  std::cout << issuer << std::endl;
  for (const auto& f: data_struct.fields()) {
    std::cout << f.first << std::endl;
  }
  std::cout << "---------" << std::endl;
  
  if (entry_it->second.struct_value().fields().empty()) {
    return false;
  }

  // Serialize the payload from Envoy jwt filter first before writing it to
  // |payload|.
  // TODO (pitlv2109): Return protobuf Struct instead of string, once Istio jwt
  // filter is removed. Also need to change how Istio authn filter processes the
  // jwt payload.
  MessageToJsonString(entry_it->second.struct_value(), payload);
  return true;
}

bool FilterContext::getJwtPayloadFromIstioJwtFilter(
    const std::string& issuer, std::string* payload) const {
  // Try getting the Jwt payload from Istio jwt-auth filter.
  auto filter_it =
      dynamic_metadata_.filter_metadata().find(IstioFilterName::kJwt);
  if (filter_it == dynamic_metadata_.filter_metadata().end()) {
    return false;
  }

  const auto& data_struct = filter_it->second;

  const auto entry_it = data_struct.fields().find(issuer);
  if (entry_it == data_struct.fields().end()) {
    return false;
  }

  if (entry_it->second.string_value().empty()) {
    return false;
  }

  *payload = entry_it->second.string_value();
  return true;
}

#ifdef NULL_PLUGIN
}  // namespace AuthN
}  // namespace null_plugin
}  // namespace proxy_wasm
#endif