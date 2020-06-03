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

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "authentication/v1alpha1/policy.pb.h"
#include "envoy/config/filter/http/authn/v2alpha1/config.pb.h"
#include "src/envoy/http/authn_wasm/connection_context.h"
#include "src/istio/authn/context.pb.h"

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"

using proxy_wasm::null_plugin::getProperty;
using proxy_wasm::null_plugin::logDebug;

namespace proxy_wasm {
namespace null_plugin {
namespace Http {
namespace AuthN {

#endif

using RawHeaderMap =
    std::vector<std::pair<absl::string_view, absl::string_view>>;
// TODO(shikugawa): use Envoy::Http::HeaderMapImpl. Because which is optimized
// to process headers.
using HeaderMap = std::unordered_map<std::string, std::string>;

// FilterContext holds inputs, such as request dynamic metadata and
// connection and result data for authentication process.
class FilterContext {
 public:
  FilterContext(
      ConnectionContext&& connection_context,
      const RawHeaderMap& raw_header_map,
      const istio::authn::Metadata& dynamic_metadata,
      const istio::envoy::config::filter::http::authn::v2alpha1::FilterConfig&
          filter_config);

  // Sets peer result based on authenticated payload. Input payload can be
  // null, which basically changes nothing.
  void setPeerResult(const istio::authn::Payload* payload);

  // Sets origin result based on authenticated payload. Input payload can be
  // null, which basically changes nothing.
  void setOriginResult(const istio::authn::Payload* payload);

  // Sets principal based on binding rule, and the existing peer and origin
  // result.
  void setPrincipal(
      const istio::authentication::v1alpha1::PrincipalBinding& binding);

  // Returns the authentication result.
  const istio::authn::Result& authenticationResult() { return result_; }

  // Accessor to the filter config
  const istio::envoy::config::filter::http::authn::v2alpha1::FilterConfig&
  filterConfig() const {
    return filter_config_;
  }

  // Gets JWT payload (output from JWT filter) for given issuer. If non-empty
  // payload found, returns true and set the output payload string. Otherwise,
  // returns false.
  absl::optional<std::string> getJwtPayload(const std::string& issuer) const;

  // Return header map.
  const HeaderMap& requestHeader() { return header_map_; }

  const ConnectionContext& connectionContext() const {
    return connection_context_;
  }

 private:
  void createHeaderMap(const RawHeaderMap& raw_header_map);

  // Helper function for getJwtPayload(). It gets the jwt payload from Envoy jwt
  // filter metadata and write to |payload|.
  absl::optional<std::string> getJwtPayloadFromEnvoyJwtFilter(
      const std::string& issuer) const;

  // Helper function for getJwtPayload(). It gets the jwt payload from Istio jwt
  // filter metadata and write to |payload|.
  absl::optional<std::string> getJwtPayloadFromIstioJwtFilter(
      const std::string& issuer) const;

  // http request header
  HeaderMap header_map_;

  // context of established connection
  const ConnectionContext& connection_context_;

  // Holds authentication attribute outputs.
  istio::authn::Result result_;

  // Store the Istio authn filter config.
  const istio::envoy::config::filter::http::authn::v2alpha1::FilterConfig&
      filter_config_;

  // Const reference to request info dynamic metadata. This provides data that
  // output from other filters, e.g JWT.
  const istio::authn::Metadata& dynamic_metadata_;
};

using FilterContextPtr = std::shared_ptr<FilterContext>;

#ifdef NULL_PLUGIN

}  // namespace AuthN
}  // namespace Http
}  // namespace null_plugin
}  // namespace proxy_wasm

#endif