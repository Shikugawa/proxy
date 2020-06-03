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

#include "authentication/v1alpha1/policy.pb.h"
#include "src/envoy/http/authn_wasm/filter_context.h"
#include "src/istio/authn/context.pb.h"

#ifdef NULL_PLUGIN

namespace proxy_wasm {
namespace null_plugin {
namespace Http {
namespace AuthN {

#endif

// AuthenticatorBase is the base class for authenticator. It provides functions
// to perform individual authentication methods, which can be used to construct
// compound authentication flow.
class AuthenticatorBase {
 public:
  AuthenticatorBase(FilterContextPtr filter_context);
  virtual ~AuthenticatorBase();

  // Perform authentication.
  virtual bool run(istio::authn::Payload*) = 0;

  // Validate TLS/MTLS connection and extract authenticated attributes (just
  // source user identity for now). Unlike mTLS, TLS connection does not require
  // a client certificate.
  virtual bool validateX509(
      const istio::authentication::v1alpha1::MutualTls& params,
      istio::authn::Payload* payload) const;

  // Validates JWT given the jwt params. If JWT is validated, it will extract
  // attributes and claims (JwtPayload), returns status SUCCESS.
  // Otherwise, returns status FAILED.
  virtual bool validateJwt(const istio::authentication::v1alpha1::Jwt& params,
                           istio::authn::Payload* payload);

  // Mutable accessor to filter context.
  FilterContextPtr filterContext() { return filter_context_; }

 private:
  bool validateTrustDomain(const ConnectionContext& connection) const;

  // Pointer to filter state. Do not own.
  FilterContextPtr filter_context_;
};

#ifdef NULL_PLUGIN

}  // namespace AuthN
}  // namespace Http
}  // namespace null_plugin
}  // namespace proxy_wasm

#endif