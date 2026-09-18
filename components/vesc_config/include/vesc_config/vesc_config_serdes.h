/*
 * Generic serialize / deserialize for a vc_table_t-described config, byte-exact
 * with VESC Tool's ConfigParams::serialize/deSerialize + VByteArray. The wire
 * format is: [uint32 signature][params in ser_order]. The firmware rejects a
 * SET whose signature doesn't match its own, so the table must match the
 * firmware version (see vesc_config_runtime auto-detect).
 */
#pragma once

#include "vesc_config/vesc_config_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime parameter values, indexed by params[] index (NOT serialize order). */
typedef union { double d; int32_t i; } vc_value_t;

/* crc32c over the serialize-order metadata (name+type+vTx+enumNames), matching
 * VESC Tool ConfigParams::getSignature. Equals table->expected_signature when
 * the generated table is intact. */
uint32_t vesc_config_signature(const vc_table_t *t);

/* Exact serialized byte count including the 4-byte signature. */
size_t vesc_config_serialized_size(const vc_table_t *t);

/* Serialize vals[] into out (must hold >= vesc_config_serialized_size()).
 * Returns the number of bytes written, or 0 on overflow. */
size_t vesc_config_serialize(const vc_table_t *t, const vc_value_t *vals,
                             uint8_t *out, size_t out_cap);

/* Deserialize a config blob (starting at the 4-byte signature, i.e. the COMM
 * payload AFTER the COMM-ID byte) into vals[]. Returns false on signature
 * mismatch or truncation. */
bool vesc_config_deserialize(const vc_table_t *t, const uint8_t *blob, size_t len,
                             vc_value_t *vals_out);

/* Seed vals[] from the table's firmware defaults. */
void vesc_config_load_defaults(const vc_table_t *t, vc_value_t *vals_out);

#ifdef __cplusplus
}
#endif
