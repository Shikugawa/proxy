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

#ifdef NULL_PLUGIN

namespace proxy_wasm {
namespace null_plugin {
namespace Http {
namespace AuthN {

#endif
// ConnectionContext::ConnectionContext() {
//   if (isTls()) {
// peer_cert_info_ = std::make_unique<TlsPeerCertificateInfo>();
// peer_cert_info_->uriSans() = getProperty({Connection,
// UriSanPeerCertificate}); local_cert_info_ =
// std::make_unique<TlsLocalCertificateInfo>(); local_cert_info_->uriSans()
// = getProperty({Connection, UriSanLocalCertificate}); mtls_ =
// getProperty({Connection, Mtls}).value_or(false);
//   }
// }

#ifdef NULL_PLUGIN

}  // namespace AuthN
}  // namespace Http
}  // namespace null_plugin
}  // namespace proxy_wasm

#endif