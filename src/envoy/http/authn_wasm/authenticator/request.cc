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

#include "absl/strings/str_cat.h"
#include "authentication/v1alpha1/policy.pb.h"
#include "common/http/headers.h"
#include "src/envoy/http/authn_wasm/authenticator/origin.h"

namespace Envoy {
namespace Wasm {
namespace Http {
namespace AuthN {

RequestAuthenticator::RequestAuthenticator(
    FilterContextPtr filter_context,
    const istio::authentication::v1alpha1::Policy& policy)
    : AuthenticatorBase(filter_context), policy_(policy) {}

RequestAuthenticator::run(istio::authn::Payload* payload) {
  if (policy_.origins_size() == 0 &&
      policy_.principal_binding() ==
          istio::authentication::v1alpha1::PrincipalBinding::USE_ORIGIN) {
    // Validation should reject policy that have rule to USE_ORIGIN but
    // does not provide any origin method so this code should
    // never reach. However, it's ok to treat it as authentication
    // fails.
    logWarn(absl::StrCat(
        "Principal is binded to origin, but no method specified in policy ",
        policy_.DebugString()));
    return false;
  }

  constexpr auto isCorsPreflightRequest =
      [](const Http::RequestHeaderMap& headers) -> bool {
    return headers.Method() &&
           headers.Method()->value().getStringView() ==
               Http::Headers::get().MethodValues.Options &&
           headers.Origin() && !headers.Origin()->value().empty() &&
           headers.AccessControlRequestMethod() &&
           !headers.AccessControlRequestMethod()->value().empty();
  };

  if (isCorsPreflightRequest(filterContext()->headerMap())) {
    // The CORS preflight doesn't include user credentials, allow regardless of
    // JWT policy. See
    // http://www.w3.org/TR/cors/#cross-origin-request-with-preflight.
    logDebug("CORS preflight request allowed regardless of JWT policy");
    return true;
  }

  
}

}  // namespace AuthN
}  // namespace Http
}  // namespace Wasm
}  // namespace Envoy