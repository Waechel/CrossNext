#pragma once
// Incremental SHA-256 that builds on both ESP-IDF 5 (Mbed TLS 3, public
// mbedtls_sha256_* context API) and ESP-IDF 6 (Mbed TLS 4, which removed it;
// hashing goes through PSA Crypto). Same shape as the mbedtls calls it replaces.
// The ESP-IDF 6 branch follows RickyOS-Plus PlusSha256.h (MIT).
#include <esp_idf_version.h>
#include <stddef.h>
#include <stdint.h>

#if ESP_IDF_VERSION_MAJOR >= 6
#include <psa/crypto.h>

struct Sha256Ctx {
  psa_hash_operation_t op;
};
inline void sha256Init(Sha256Ctx* ctx) {
  const psa_hash_operation_t initial = PSA_HASH_OPERATION_INIT;
  ctx->op = initial;
}
inline void sha256Free(Sha256Ctx* ctx) { (void)psa_hash_abort(&ctx->op); }
inline int sha256Starts(Sha256Ctx* ctx) {
  psa_status_t s = psa_crypto_init();
  if (s != PSA_SUCCESS) return (int)s;
  s = psa_hash_abort(&ctx->op);
  if (s != PSA_SUCCESS) return (int)s;
  return (int)psa_hash_setup(&ctx->op, PSA_ALG_SHA_256);
}
inline int sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
  return (int)psa_hash_update(&ctx->op, data, len);
}
inline int sha256Finish(Sha256Ctx* ctx, uint8_t out[32]) {
  size_t n = 0;
  return (int)psa_hash_finish(&ctx->op, out, 32, &n);
}
#else
#include <mbedtls/sha256.h>

struct Sha256Ctx {
  mbedtls_sha256_context c;
};
inline void sha256Init(Sha256Ctx* ctx) { mbedtls_sha256_init(&ctx->c); }
inline void sha256Free(Sha256Ctx* ctx) { mbedtls_sha256_free(&ctx->c); }
inline int sha256Starts(Sha256Ctx* ctx) { return mbedtls_sha256_starts(&ctx->c, /*is224=*/0); }
inline int sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
  return mbedtls_sha256_update(&ctx->c, data, len);
}
inline int sha256Finish(Sha256Ctx* ctx, uint8_t out[32]) { return mbedtls_sha256_finish(&ctx->c, out); }
#endif
