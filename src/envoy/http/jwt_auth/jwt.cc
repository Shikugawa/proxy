/* Copyright 2017 Istio Authors. All Rights Reserved.
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

#include "jwt.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/utility.h"
#include "google/protobuf/util/json_util.h"
#include "openssl/bn.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"
#include "openssl/sha.h"

namespace Envoy {
namespace Http {
namespace JwtAuth {

std::string StatusToString(Status status) {
  static std::map<Status, std::string> table = {
      {Status::OK, "OK"},
      {Status::JWT_MISSED, "Required JWT token is missing"},
      {Status::JWT_EXPIRED, "JWT is expired"},
      {Status::JWT_BAD_FORMAT, "JWT_BAD_FORMAT"},
      {Status::JWT_HEADER_PARSE_ERROR, "JWT_HEADER_PARSE_ERROR"},
      {Status::JWT_HEADER_NO_ALG, "JWT_HEADER_NO_ALG"},
      {Status::JWT_HEADER_BAD_ALG, "JWT_HEADER_BAD_ALG"},
      {Status::JWT_SIGNATURE_PARSE_ERROR, "JWT_SIGNATURE_PARSE_ERROR"},
      {Status::JWT_INVALID_SIGNATURE, "JWT_INVALID_SIGNATURE"},
      {Status::JWT_PAYLOAD_PARSE_ERROR, "JWT_PAYLOAD_PARSE_ERROR"},
      {Status::JWT_HEADER_BAD_KID, "JWT_HEADER_BAD_KID"},
      {Status::JWT_UNKNOWN_ISSUER, "Unknown issuer"},
      {Status::JWK_PARSE_ERROR, "JWK_PARSE_ERROR"},
      {Status::JWK_NO_KEYS, "JWK_NO_KEYS"},
      {Status::JWK_BAD_KEYS, "JWK_BAD_KEYS"},
      {Status::JWK_NO_VALID_PUBKEY, "JWK_NO_VALID_PUBKEY"},
      {Status::KID_ALG_UNMATCH, "KID_ALG_UNMATCH"},
      {Status::ALG_NOT_IMPLEMENTED, "ALG_NOT_IMPLEMENTED"},
      {Status::PEM_PUBKEY_BAD_BASE64, "PEM_PUBKEY_BAD_BASE64"},
      {Status::PEM_PUBKEY_PARSE_ERROR, "PEM_PUBKEY_PARSE_ERROR"},
      {Status::JWK_RSA_PUBKEY_PARSE_ERROR, "JWK_RSA_PUBKEY_PARSE_ERROR"},
      {Status::FAILED_CREATE_EC_KEY, "FAILED_CREATE_EC_KEY"},
      {Status::JWK_EC_PUBKEY_PARSE_ERROR, "JWK_EC_PUBKEY_PARSE_ERROR"},
      {Status::FAILED_CREATE_ECDSA_SIGNATURE, "FAILED_CREATE_ECDSA_SIGNATURE"},
      {Status::AUDIENCE_NOT_ALLOWED, "Audience doesn't match"},
      {Status::FAILED_FETCH_PUBKEY, "Failed to fetch public key"},
  };
  return table[status];
}

namespace {

// Conversion table is taken from
// https://opensource.apple.com/source/QuickTimeStreamingServer/QuickTimeStreamingServer-452/CommonUtilitiesLib/base64.c
//
// and modified the position of 62 ('+' to '-') and 63 ('/' to '_')
const uint8_t kReverseLookupTableBase64Url[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 64, 64, 64, 64, 64, 64, 64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64,
    63, 64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64};

bool IsNotBase64UrlChar(int8_t c) {
  return kReverseLookupTableBase64Url[static_cast<int32_t>(c)] & 64;
}

}  // namespace

std::string Base64UrlDecode(std::string input) {
  // allow at most 2 padding letters at the end of the input, only if input
  // length is divisible by 4
  int len = input.length();
  if (len % 4 == 0) {
    if (input[len - 1] == '=') {
      input.pop_back();
      if (input[len - 2] == '=') {
        input.pop_back();
      }
    }
  }
  // if input contains non-base64url character, return empty string
  // Note: padding letter must not be contained
  if (std::find_if(input.begin(), input.end(), IsNotBase64UrlChar) !=
      input.end()) {
    return "";
  }

  // base64url is using '-', '_' instead of '+', '/' in base64 string.
  std::replace(input.begin(), input.end(), '-', '+');
  std::replace(input.begin(), input.end(), '_', '/');

  // base64 string should be padded with '=' so as to the length of the string
  // is divisible by 4.
  switch (input.length() % 4) {
    case 0:
      break;
    case 2:
      input += "==";
      break;
    case 3:
      input += "=";
      break;
    default:
      // * an invalid base64url input. return empty string.
      return "";
  }
  return Base64::decode(input);
}

namespace {

const uint8_t *CastToUChar(const std::string &str) {
  return reinterpret_cast<const uint8_t *>(str.c_str());
}

// Class to create EVP_PKEY object from string of public key, formatted in PEM
// or JWKs.
// If it failed, status_ holds the failure reason.
//
// Usage example:
//   EvpPkeyGetter e;
//   bssl::UniquePtr<EVP_PKEY> pkey =
//   e.EvpPkeyFromStr(pem_formatted_public_key);
// (You can use EvpPkeyFromJwkRSA() or EcKeyFromJwkEC() for JWKs)
class EvpPkeyGetter : public WithStatus {
 public:
  EvpPkeyGetter() {}

  bssl::UniquePtr<EVP_PKEY> EvpPkeyFromStr(const std::string &pkey_pem) {
    std::string pkey_der = Base64::decode(pkey_pem);
    if (pkey_der == "") {
      UpdateStatus(Status::PEM_PUBKEY_BAD_BASE64);
      return nullptr;
    }
    auto rsa = bssl::UniquePtr<RSA>(
        RSA_public_key_from_bytes(CastToUChar(pkey_der), pkey_der.length()));
    if (!rsa) {
      UpdateStatus(Status::PEM_PUBKEY_PARSE_ERROR);
    }
    return EvpPkeyFromRsa(rsa.get());
  }

  bssl::UniquePtr<EVP_PKEY> EvpPkeyFromJwkRSA(const std::string &n,
                                              const std::string &e) {
    return EvpPkeyFromRsa(RsaFromJwk(n, e).get());
  }

  bssl::UniquePtr<EC_KEY> EcKeyFromJwkEC(const std::string &x,
                                         const std::string &y) {
    bssl::UniquePtr<EC_KEY> ec_key(
        EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
    if (!ec_key) {
      UpdateStatus(Status::FAILED_CREATE_EC_KEY);
      return nullptr;
    }
    bssl::UniquePtr<BIGNUM> bn_x = BigNumFromBase64UrlString(x);
    bssl::UniquePtr<BIGNUM> bn_y = BigNumFromBase64UrlString(y);
    if (!bn_x || !bn_y) {
      // EC public key field is missing or has parse error.
      UpdateStatus(Status::JWK_EC_PUBKEY_PARSE_ERROR);
      return nullptr;
    }

    if (EC_KEY_set_public_key_affine_coordinates(ec_key.get(), bn_x.get(),
                                                 bn_y.get()) == 0) {
      UpdateStatus(Status::JWK_EC_PUBKEY_PARSE_ERROR);
      return nullptr;
    }
    return ec_key;
  }

 private:
  // In the case where rsa is nullptr, UpdateStatus() should be called
  // appropriately elsewhere.
  bssl::UniquePtr<EVP_PKEY> EvpPkeyFromRsa(RSA *rsa) {
    if (!rsa) {
      return nullptr;
    }
    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
    EVP_PKEY_set1_RSA(key.get(), rsa);
    return key;
  }

  bssl::UniquePtr<BIGNUM> BigNumFromBase64UrlString(const std::string &s) {
    std::string s_decoded = Base64UrlDecode(s);
    if (s_decoded == "") {
      return nullptr;
    }
    return bssl::UniquePtr<BIGNUM>(
        BN_bin2bn(CastToUChar(s_decoded), s_decoded.length(), NULL));
  };

  bssl::UniquePtr<RSA> RsaFromJwk(const std::string &n, const std::string &e) {
    bssl::UniquePtr<RSA> rsa(RSA_new());
    // It crash if RSA object couldn't be created.
    assert(rsa);

    rsa->n = BigNumFromBase64UrlString(n).release();
    rsa->e = BigNumFromBase64UrlString(e).release();
    if (!rsa->n || !rsa->e) {
      // RSA public key field is missing or has parse error.
      UpdateStatus(Status::JWK_RSA_PUBKEY_PARSE_ERROR);
      return nullptr;
    }
    return rsa;
  }
};

template <typename T>
absl::optional<std::vector<T>> getProtoListValue(const ProtobufMapType &,
                                                 std::string) {
  static_assert(true, "Unsupported Type");
}

template <>
absl::optional<std::vector<std::string>> getProtoListValue(
    const ProtobufMapType &struct_value, std::string key) {
  const auto field_iter = struct_value.find(key);
  if (field_iter == struct_value.end()) {
    return absl::nullopt;
  }
  std::vector<std::string> list_values;
  for (const auto &value : field_iter->second.list_value().values()) {
    if (value.kind_case() != google::protobuf::Value::KindCase::kStringValue) {
      return absl::nullopt;
    }
    list_values.emplace_back(value.string_value());
  }
  return list_values;
}

template <>
absl::optional<std::vector<google::protobuf::Struct>> getProtoListValue(
    const ProtobufMapType &struct_value, std::string key) {
  const auto field_iter = struct_value.find(key);
  if (field_iter == struct_value.end()) {
    return absl::nullopt;
  }
  std::vector<google::protobuf::Struct> list_values;
  for (const auto &value : field_iter->second.list_value().values()) {
    if (value.kind_case() != google::protobuf::Value::KindCase::kStructValue) {
      return absl::nullopt;
    }
    list_values.emplace_back(value.struct_value());
  }
  return list_values;
}

template <typename T>
absl::optional<T> getProtoMapValue(const ProtobufMapType &, std::string) {
  static_assert(true, "Unsupported Type");
}

template <>
absl::optional<std::string> getProtoMapValue(
    const ProtobufMapType &struct_value, std::string key) {
  const auto field_iter = struct_value.find(key);
  if (field_iter == struct_value.end() ||
      field_iter->second.kind_case() !=
          google::protobuf::Value::KindCase::kStringValue) {
    return absl::nullopt;
  }
  return field_iter->second.string_value();
}

template <>
absl::optional<uint64_t> getProtoMapValue(const ProtobufMapType &struct_value,
                                          std::string key) {
  const auto field_iter = struct_value.find(key);
  if (field_iter == struct_value.end() ||
      field_iter->second.kind_case() !=
          google::protobuf::Value::KindCase::kNumberValue) {
    return absl::nullopt;
  }
  return field_iter->second.number_value();
}

}  // namespace

Jwt::Jwt(const std::string &jwt) {
  // jwt must have exactly 2 dots
  if (std::count(jwt.begin(), jwt.end(), '.') != 2) {
    UpdateStatus(Status::JWT_BAD_FORMAT);
    return;
  }
  auto jwt_split = StringUtil::splitToken(jwt, ".");
  if (jwt_split.size() != 3) {
    UpdateStatus(Status::JWT_BAD_FORMAT);
    return;
  }

  // Parse header json
  header_str_base64url_ = std::string(jwt_split[0].begin(), jwt_split[0].end());
  header_str_ = Base64UrlDecode(header_str_base64url_);

  auto status =
      google::protobuf::util::JsonStringToMessage(header_str_, &header_);
  if (!status.ok()) {
    UpdateStatus(Status::JWT_HEADER_PARSE_ERROR);
    return;
  }

  // Header should contain "alg".
  const auto header_fields = header_.fields();
  const auto alg_field_iter = header_fields.find("alg");

  if (alg_field_iter == header_fields.end()) {
    UpdateStatus(Status::JWT_HEADER_NO_ALG);
    return;
  }

  if (alg_field_iter->second.kind_case() !=
      google::protobuf::Value::kStringValue) {
    UpdateStatus(Status::JWT_HEADER_BAD_ALG);
    return;
  }

  alg_ = alg_field_iter->second.string_value();
  if (alg_ != "RS256" && alg_ != "ES256" && alg_ != "RS384" &&
      alg_ != "RS512") {
    UpdateStatus(Status::ALG_NOT_IMPLEMENTED);
    return;
  }

  // Header may contain "kid", which should be a string if exists.
  const auto kid_iter = header_fields.find("kid");
  if (kid_iter != header_fields.end() &&
      kid_iter->second.kind_case() != google::protobuf::Value::kStringValue) {
    UpdateStatus(Status::JWT_HEADER_BAD_KID);
    return;
  }

  kid_ = kid_iter != header_fields.end() ? kid_iter->second.string_value() : "";

  // Parse payload json
  payload_str_base64url_ =
      std::string(jwt_split[1].begin(), jwt_split[1].end());
  payload_str_ = Base64UrlDecode(payload_str_base64url_);

  status = google::protobuf::util::JsonStringToMessage(payload_str_, &payload_);

  if (!status.ok()) {
    UpdateStatus(Status::JWT_PAYLOAD_PARSE_ERROR);
    return;
  }

  const auto payload_fields = payload_.fields();

  iss_ = getProtoMapValue<std::string>(payload_fields, "iss").value_or("");
  sub_ = getProtoMapValue<std::string>(payload_fields, "sub").value_or("");
  exp_ = getProtoMapValue<uint64_t>(payload_fields, "exp").value_or(0);

  // "aud" can be either string array or string.
  // Try as string array, read it as empty array if doesn't exist.
<<<<<<< HEAD
<<<<<<< HEAD
  auto actual_list_aud = getProtoListValue<std::string>(payload_fields, "aud");
  if (actual_list_aud.has_value()) {
    aud_ = actual_list_aud.value();
  } else {
    auto actual_str_aud = getProtoMapValue<std::string>(payload_fields, "aud");
    if (actual_str_aud.has_value()) aud_.emplace_back(actual_str_aud.value());
=======
=======
>>>>>>> e598b67b... fix
  if (!Wasm::Common::JsonArrayIterate(
          payload_, "aud", [&](const Wasm::Common::JsonObject &obj) -> bool {
            auto str_obj_result = Wasm::Common::JsonValueAs<std::string>(obj);
            if (str_obj_result.second !=
                Wasm::Common::JsonParserResultDetail::OK) {
              return false;
            }
            aud_.emplace_back(str_obj_result.first.value());
            return true;
          })) {
    auto aud_field = Wasm::Common::JsonGetField<std::string>(payload_, "aud");
    if (aud_field.detail() != Wasm::Common::JsonParserResultDetail::OK) {
      UpdateStatus(Status::JWT_PAYLOAD_PARSE_ERROR);
      return;
    }
    aud_.emplace_back(aud_field.fetch());
>>>>>>> e598b67b... fix
  }

  // Set up signature
  signature_ =
      Base64UrlDecode(std::string(jwt_split[2].begin(), jwt_split[2].end()));
  if (signature_ == "") {
    // Signature is a bad Base64url input.
    UpdateStatus(Status::JWT_SIGNATURE_PARSE_ERROR);
    return;
  }
}

bool Verifier::VerifySignatureRSA(EVP_PKEY *key, const EVP_MD *md,
                                  const uint8_t *signature,
                                  size_t signature_len,
                                  const uint8_t *signed_data,
                                  size_t signed_data_len) {
  bssl::UniquePtr<EVP_MD_CTX> md_ctx(EVP_MD_CTX_create());

  if (EVP_DigestVerifyInit(md_ctx.get(), nullptr, md, nullptr, key) != 1) {
    return false;
  }
  if (EVP_DigestVerifyUpdate(md_ctx.get(), signed_data, signed_data_len) != 1) {
    return false;
  }
  return (EVP_DigestVerifyFinal(md_ctx.get(), signature, signature_len) == 1);
}

bool Verifier::VerifySignatureRSA(EVP_PKEY *key, const EVP_MD *md,
                                  const std::string &signature,
                                  const std::string &signed_data) {
  return VerifySignatureRSA(key, md, CastToUChar(signature), signature.length(),
                            CastToUChar(signed_data), signed_data.length());
}

bool Verifier::VerifySignatureEC(EC_KEY *key, const uint8_t *signature,
                                 size_t signature_len,
                                 const uint8_t *signed_data,
                                 size_t signed_data_len) {
  // ES256 signature should be 64 bytes.
  if (signature_len != 2 * 32) {
    return false;
  }

  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(signed_data, signed_data_len, digest);

  bssl::UniquePtr<ECDSA_SIG> ecdsa_sig(ECDSA_SIG_new());
  if (!ecdsa_sig) {
    UpdateStatus(Status::FAILED_CREATE_ECDSA_SIGNATURE);
    return false;
  }

  BN_bin2bn(signature, 32, ecdsa_sig->r);
  BN_bin2bn(signature + 32, 32, ecdsa_sig->s);
  return (ECDSA_do_verify(digest, SHA256_DIGEST_LENGTH, ecdsa_sig.get(), key) ==
          1);
}

bool Verifier::VerifySignatureEC(EC_KEY *key, const std::string &signature,
                                 const std::string &signed_data) {
  return VerifySignatureEC(key, CastToUChar(signature), signature.length(),
                           CastToUChar(signed_data), signed_data.length());
}

bool Verifier::Verify(const Jwt &jwt, const Pubkeys &pubkeys) {
  // If JWT status is not OK, inherits its status and return false.
  if (jwt.GetStatus() != Status::OK) {
    UpdateStatus(jwt.GetStatus());
    return false;
  }

  // If pubkeys status is not OK, inherits its status and return false.
  if (pubkeys.GetStatus() != Status::OK) {
    UpdateStatus(pubkeys.GetStatus());
    return false;
  }

  std::string signed_data =
      jwt.header_str_base64url_ + '.' + jwt.payload_str_base64url_;
  bool kid_alg_matched = false;
  for (auto &pubkey : pubkeys.keys_) {
    // If kid is specified in JWT, JWK with the same kid is used for
    // verification.
    // If kid is not specified in JWT, try all JWK.
    if (jwt.kid_ != "" && pubkey->kid_specified_ && pubkey->kid_ != jwt.kid_) {
      continue;
    }

    // The same alg must be used.
    if (pubkey->alg_specified_ && pubkey->alg_ != jwt.alg_) {
      continue;
    }
    kid_alg_matched = true;

    if (pubkey->kty_ == "EC" &&
        VerifySignatureEC(pubkey->ec_key_.get(), jwt.signature_, signed_data)) {
      // Verification succeeded.
      return true;
    } else if (pubkey->pem_format_ || pubkey->kty_ == "RSA") {
      const EVP_MD *md;
      if (jwt.alg_ == "RS384") {
        md = EVP_sha384();
      } else if (jwt.alg_ == "RS512") {
        md = EVP_sha512();
      } else {
        // default to SHA256
        md = EVP_sha256();
      }
      if (VerifySignatureRSA(pubkey->evp_pkey_.get(), md, jwt.signature_,
                             signed_data)) {
        // Verification succeeded.
        return true;
      }
    }
  }
  // Verification failed.
  if (kid_alg_matched) {
    UpdateStatus(Status::JWT_INVALID_SIGNATURE);
  } else {
    UpdateStatus(Status::KID_ALG_UNMATCH);
  }
  return false;
}

// Returns the parsed header.
google::protobuf::Struct &Jwt::Header() { return header_; }

const std::string &Jwt::HeaderStr() { return header_str_; }
const std::string &Jwt::HeaderStrBase64Url() { return header_str_base64url_; }
const std::string &Jwt::Alg() { return alg_; }
const std::string &Jwt::Kid() { return kid_; }

// Returns payload JSON.
google::protobuf::Struct &Jwt::Payload() { return payload_; }

const std::string &Jwt::PayloadStr() { return payload_str_; }
const std::string &Jwt::PayloadStrBase64Url() { return payload_str_base64url_; }
const std::string &Jwt::Iss() { return iss_; }
const std::vector<std::string> &Jwt::Aud() { return aud_; }
const std::string &Jwt::Sub() { return sub_; }
int64_t Jwt::Exp() { return exp_; }

void Pubkeys::CreateFromPemCore(const std::string &pkey_pem) {
  keys_.clear();
  std::unique_ptr<Pubkey> key_ptr(new Pubkey());
  EvpPkeyGetter e;
  key_ptr->evp_pkey_ = e.EvpPkeyFromStr(pkey_pem);
  key_ptr->pem_format_ = true;
  UpdateStatus(e.GetStatus());
  if (e.GetStatus() == Status::OK) {
    keys_.emplace_back(std::move(key_ptr));
  }
}

void Pubkeys::CreateFromJwksCore(const std::string &pkey_jwks) {
  keys_.clear();

  google::protobuf::Struct jwks_object;
  auto status =
      google::protobuf::util::JsonStringToMessage(pkey_jwks, &jwks_object);
  if (!status.ok()) {
    UpdateStatus(Status::JWK_PARSE_ERROR);
    return;
  }

  const auto jwks_field = jwks_object.fields();
  const auto keys_iter = jwks_field.find("keys");

  if (keys_iter == jwks_field.end()) {
    UpdateStatus(Status::JWK_NO_KEYS);
    return;
  }

<<<<<<< HEAD
<<<<<<< HEAD
  auto actual_keys =
      getProtoListValue<google::protobuf::Struct>(jwks_field, "keys");
  if (!actual_keys.has_value()) {
=======
=======
>>>>>>> e598b67b... fix
  if (!Wasm::Common::JsonArrayIterate(
          jwks_json, "keys", [&](const Wasm::Common::JsonObject &obj) -> bool {
            key_refs.emplace_back(
                std::reference_wrapper<const Wasm::Common::JsonObject>(obj));
            return true;
          })) {
>>>>>>> e598b67b... fix
    UpdateStatus(Status::JWK_BAD_KEYS);
    return;
  }

  for (const auto &jwk_field : actual_keys.value()) {
    if (!ExtractPubkeyFromJwk(jwk_field.fields())) {
      continue;
    }
  }

  if (keys_.size() == 0) {
    UpdateStatus(Status::JWK_NO_VALID_PUBKEY);
  }
}

bool Pubkeys::ExtractPubkeyFromJwk(const ProtobufMapType &jwk_field) {
  // Check "kty" parameter, it should exist.
  // https://tools.ietf.org/html/rfc7517#section-4.1
  // If "kty" is missing, getString throws an exception.
  auto kty = getProtoMapValue<std::string>(jwk_field, "kty");
  if (!kty.has_value()) {
    return false;
  }

  // Extract public key according to "kty" value.
  // https://tools.ietf.org/html/rfc7518#section-6.1
  if (kty.value() == "EC") {
    return ExtractPubkeyFromJwkEC(jwk_field);
  } else if (kty.value() == "RSA") {
    return ExtractPubkeyFromJwkRSA(jwk_field);
  }

  return false;
}

bool Pubkeys::ExtractPubkeyFromJwkRSA(const ProtobufMapType &jwk_field) {
  std::unique_ptr<Pubkey> pubkey(new Pubkey());

  // "kid" and "alg" are optional, if they do not exist, set them to "".
  // https://tools.ietf.org/html/rfc7517#page-8
  if (jwk_field.find("kid") != jwk_field.end()) {
    auto actual_kid = getProtoMapValue<std::string>(jwk_field, "kid");
    if (!actual_kid.has_value()) {
      return false;
    }
    pubkey->kid_ = actual_kid.value();
    pubkey->kid_specified_ = true;
  }

  if (jwk_field.find("alg") != jwk_field.end()) {
    auto actual_alg = getProtoMapValue<std::string>(jwk_field, "alg");
    // Allow only "RS" prefixed algorithms.
    // https://tools.ietf.org/html/rfc7518#section-3.1
    if (!actual_alg.has_value() ||
        !(actual_alg.value() == "RS256" || actual_alg.value() == "RS384" ||
          actual_alg.value() == "RS512")) {
      return false;
    }
    pubkey->alg_ = actual_alg.value();
    pubkey->alg_specified_ = true;
  }

  auto actual_kty = getProtoMapValue<std::string>(jwk_field, "kty");
  assert(actual_kty.has_value());

  pubkey->kty_ = actual_kty.value();

  auto n_str = getProtoMapValue<std::string>(jwk_field, "n");
  auto e_str = getProtoMapValue<std::string>(jwk_field, "e");

  if (!n_str.has_value() || !e_str.has_value()) {
    return false;
  }

  EvpPkeyGetter e;
  pubkey->evp_pkey_ = e.EvpPkeyFromJwkRSA(n_str.value(), e_str.value());
  if (e.GetStatus() == Status::OK) {
    keys_.emplace_back(std::move(pubkey));
  } else {
    UpdateStatus(e.GetStatus());
  }

  return true;
}

bool Pubkeys::ExtractPubkeyFromJwkEC(const ProtobufMapType &jwk_field) {
  std::unique_ptr<Pubkey> pubkey(new Pubkey());

  if (jwk_field.find("kid") != jwk_field.end()) {
    auto actual_kid = getProtoMapValue<std::string>(jwk_field, "kid");
    if (!actual_kid.has_value()) {
      return false;
    }
    pubkey->kid_ = actual_kid.value();
    pubkey->kid_specified_ = true;
  }

  if (jwk_field.find("alg") != jwk_field.end()) {
    auto actual_alg = getProtoMapValue<std::string>(jwk_field, "alg");
    if (!actual_alg.has_value() || actual_alg.value() != "ES256") {
      return false;
    }
    pubkey->alg_ = actual_alg.value();
    pubkey->alg_specified_ = true;
  }

  auto actual_kty = getProtoMapValue<std::string>(jwk_field, "kty");
  if (!actual_kty.has_value()) {
    return false;
  }
  pubkey->kty_ = actual_kty.value();

  auto x_str = getProtoMapValue<std::string>(jwk_field, "x");
  auto y_str = getProtoMapValue<std::string>(jwk_field, "y");

  if (!x_str.has_value() || !y_str.has_value()) {
    return false;
  }

  EvpPkeyGetter e;
  pubkey->ec_key_ = e.EcKeyFromJwkEC(x_str.value(), y_str.value());
  if (e.GetStatus() == Status::OK) {
    keys_.emplace_back(std::move(pubkey));
  } else {
    UpdateStatus(e.GetStatus());
  }

  return true;
}

std::unique_ptr<Pubkeys> Pubkeys::CreateFrom(const std::string &pkey,
                                             Type type) {
  std::unique_ptr<Pubkeys> keys(new Pubkeys());
  switch (type) {
    case Type::JWKS:
      keys->CreateFromJwksCore(pkey);
      break;
    case Type::PEM:
      keys->CreateFromPemCore(pkey);
      break;
    default:
      PANIC("can not reach here");
  }
  return keys;
}

}  // namespace JwtAuth
}  // namespace Http
}  // namespace Envoy