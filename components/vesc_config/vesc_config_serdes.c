/*
 * Generic config (de)serializer, byte-exact with VESC Tool's
 * ConfigParams::serialize/deSerialize + VByteArray. See vesc_config_serdes.h.
 *
 * Two subtleties that must match the reference exactly:
 *   - DOUBLE16/DOUBLE32 round half-away-from-zero (VByteArray::roundDouble),
 *     NOT truncate like buffer_append_float16/32 — hence vc_round() below.
 *   - ENUM/BOOL/BITFIELD are always 1 byte on the wire regardless of vTx
 *     (ConfigParams::getParamSerial appends int8).
 */
#include "vesc_config/vesc_config_serdes.h"

#include "vesc_can/buffer.h"
#include "vesc_can/crc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Half-away-from-zero rounding, matching VByteArray::roundDouble. */
static inline int32_t vc_round(double x)
{
    return (int32_t)(x < 0.0 ? ceil(x - 0.5) : floor(x + 0.5));
}

static size_t param_wire_bytes(const vc_param_t *p)
{
    switch (p->type) {
    case VC_T_DOUBLE:
        return (p->vtx == VC_TX_DOUBLE16) ? 2u : 4u;
    case VC_T_INT:
        switch (p->vtx) {
        case VC_TX_UINT8:
        case VC_TX_INT8:   return 1u;
        case VC_TX_UINT16:
        case VC_TX_INT16:  return 2u;
        case VC_TX_UINT32:
        case VC_TX_INT32:  return 4u;
        default:           return 0u;
        }
    case VC_T_ENUM:
    case VC_T_BOOL:
    case VC_T_BITFIELD:
        return 1u;
    default:
        return 0u;  /* QSTRING / undefined never appear in ser_order */
    }
}

uint32_t vesc_config_signature(const vc_table_t *t)
{
    uint32_t crc = crc32c_init();
    char numbuf[12];

    for (uint16_t k = 0; k < t->ser_count; k++) {
        const vc_param_t *p = &t->params[t->ser_order[k]];

        const char *name = vc_str(t, p->name_off);
        crc = crc32c_update(crc, (const uint8_t *)name, (uint32_t)strlen(name));

        int n = snprintf(numbuf, sizeof numbuf, "%d", (int)p->type);
        crc = crc32c_update(crc, (const uint8_t *)numbuf, (uint32_t)n);
        n = snprintf(numbuf, sizeof numbuf, "%d", (int)p->vtx);
        crc = crc32c_update(crc, (const uint8_t *)numbuf, (uint32_t)n);

        for (uint8_t e = 0; e < p->enum_count; e++) {
            const char *en = vc_str(t, t->enum_name_offsets[p->enum_off + e]);
            crc = crc32c_update(crc, (const uint8_t *)en, (uint32_t)strlen(en));
        }
    }
    return crc32c_final(crc);
}

size_t vesc_config_serialized_size(const vc_table_t *t)
{
    size_t total = 4;  /* uint32 signature */
    for (uint16_t k = 0; k < t->ser_count; k++) {
        total += param_wire_bytes(&t->params[t->ser_order[k]]);
    }
    return total;
}

size_t vesc_config_serialize(const vc_table_t *t, const vc_value_t *vals,
                             uint8_t *out, size_t out_cap)
{
    size_t need = vesc_config_serialized_size(t);
    if (out_cap < need) {
        return 0;
    }

    int32_t ind = 0;
    buffer_append_uint32(out, vesc_config_signature(t), &ind);

    for (uint16_t k = 0; k < t->ser_count; k++) {
        uint16_t idx = t->ser_order[k];
        const vc_param_t *p = &t->params[idx];
        const vc_value_t v = vals[idx];

        switch (p->type) {
        case VC_T_DOUBLE:
            if (p->vtx == VC_TX_DOUBLE16) {
                buffer_append_int16(out, (int16_t)vc_round(v.d * p->vtx_scale), &ind);
            } else if (p->vtx == VC_TX_DOUBLE32) {
                buffer_append_int32(out, vc_round(v.d * p->vtx_scale), &ind);
            } else { /* VC_TX_DOUBLE32_AUTO */
                buffer_append_float32_auto(out, (float)v.d, &ind);
            }
            break;

        case VC_T_INT:
            switch (p->vtx) {
            case VC_TX_UINT8:
            case VC_TX_INT8:
                out[ind++] = (uint8_t)v.i;
                break;
            case VC_TX_UINT16:
                buffer_append_uint16(out, (uint16_t)v.i, &ind);
                break;
            case VC_TX_INT16:
                buffer_append_int16(out, (int16_t)v.i, &ind);
                break;
            case VC_TX_UINT32:
                buffer_append_uint32(out, (uint32_t)v.i, &ind);
                break;
            case VC_TX_INT32:
                buffer_append_int32(out, v.i, &ind);
                break;
            default:
                break;
            }
            break;

        case VC_T_ENUM:
        case VC_T_BOOL:
        case VC_T_BITFIELD:
            out[ind++] = (uint8_t)v.i;
            break;

        default:
            break;
        }
    }
    return (size_t)ind;
}

bool vesc_config_deserialize(const vc_table_t *t, const uint8_t *blob, size_t len,
                             vc_value_t *vals_out)
{
    if (len < 4) {
        return false;
    }
    int32_t ind = 0;
    uint32_t sig = buffer_get_uint32(blob, &ind);
    if (sig != vesc_config_signature(t)) {
        return false;
    }

    for (uint16_t k = 0; k < t->ser_count; k++) {
        uint16_t idx = t->ser_order[k];
        const vc_param_t *p = &t->params[idx];

        size_t need = param_wire_bytes(p);
        if ((size_t)ind + need > len) {
            return false;  /* truncated frame */
        }

        switch (p->type) {
        case VC_T_DOUBLE:
            if (p->vtx == VC_TX_DOUBLE16) {
                vals_out[idx].d = (double)buffer_get_int16(blob, &ind) / p->vtx_scale;
            } else if (p->vtx == VC_TX_DOUBLE32) {
                vals_out[idx].d = (double)buffer_get_int32(blob, &ind) / p->vtx_scale;
            } else { /* VC_TX_DOUBLE32_AUTO */
                vals_out[idx].d = (double)buffer_get_float32_auto(blob, &ind);
            }
            break;

        case VC_T_INT:
            switch (p->vtx) {
            case VC_TX_UINT8:
                vals_out[idx].i = (int32_t)(uint8_t)blob[ind++];
                break;
            case VC_TX_INT8:
                vals_out[idx].i = (int32_t)(int8_t)blob[ind++];
                break;
            case VC_TX_UINT16:
                vals_out[idx].i = (int32_t)buffer_get_uint16(blob, &ind);
                break;
            case VC_TX_INT16:
                vals_out[idx].i = (int32_t)buffer_get_int16(blob, &ind);
                break;
            case VC_TX_UINT32:
                vals_out[idx].i = (int32_t)buffer_get_uint32(blob, &ind);
                break;
            case VC_TX_INT32:
                vals_out[idx].i = buffer_get_int32(blob, &ind);
                break;
            default:
                break;
            }
            break;

        case VC_T_ENUM:
        case VC_T_BOOL:
            vals_out[idx].i = (int32_t)(int8_t)blob[ind++];   /* vbPopFrontInt8 */
            break;

        case VC_T_BITFIELD:
            vals_out[idx].i = (int32_t)(uint8_t)blob[ind++];  /* vbPopFrontUint8 */
            break;

        default:
            break;
        }
    }
    return true;
}

void vesc_config_load_defaults(const vc_table_t *t, vc_value_t *vals_out)
{
    for (uint16_t i = 0; i < t->param_count; i++) {
        const vc_param_t *p = &t->params[i];
        if (p->type == VC_T_DOUBLE) {
            vals_out[i].d = p->def.d;
        } else {
            vals_out[i].i = p->def.i;
        }
    }
}
