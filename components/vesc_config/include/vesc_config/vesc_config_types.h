/*
 * On-device representation of a VESC controller-config parameter set
 * (mcconf / appconf), mirroring VESC Tool's ConfigParams. The concrete tables
 * are auto-generated from the official parameter XML by tools/gen_vesc_config.py
 * into the generated/ tables — keep this struct layout in sync with that emitter.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parameter data type — values match VESC Tool datatypes.h CFG_T. */
typedef enum {
    VC_T_UNDEFINED = 0,
    VC_T_DOUBLE    = 1,
    VC_T_INT       = 2,
    VC_T_QSTRING   = 3,
    VC_T_ENUM      = 4,
    VC_T_BOOL      = 5,
    VC_T_BITFIELD  = 6,
} vc_type_t;

/* Wire/transmit encoding — values match VESC Tool datatypes.h VESC_TX_T. */
typedef enum {
    VC_TX_UNDEFINED     = 0,
    VC_TX_UINT8         = 1,
    VC_TX_INT8          = 2,
    VC_TX_UINT16        = 3,
    VC_TX_INT16         = 4,
    VC_TX_UINT32        = 5,
    VC_TX_INT32         = 6,
    VC_TX_DOUBLE16      = 7,
    VC_TX_DOUBLE32      = 8,
    VC_TX_DOUBLE32_AUTO = 9,
} vc_tx_t;

#define VC_FLAG_EDIT_PCT   0x01u  /* edit/display as percentage of max */
#define VC_FLAG_SHOW_DISP  0x02u  /* showDisplay graphical widget hint */
#define VC_FLAG_TRANSMIT   0x04u  /* transmittable (read/write over the wire) */

/* One parameter descriptor. Strings are offsets into the table's str_pool;
 * offset 0 is the empty string. enum_off indexes enum_name_offsets[]. */
typedef struct {
    uint32_t name_off;       /* serialize name (e.g. "l_current_max") */
    uint32_t longname_off;   /* UI label */
    uint32_t suffix_off;     /* unit suffix, e.g. " A"; 0 == none */
    float    min;
    float    max;
    float    step;
    float    editor_scale;   /* divide raw value by this for display */
    double   vtx_scale;      /* vTxDoubleScale — used for DOUBLE16/32 (de)serialize */
    union { double d; int32_t i; } def;  /* firmware default value */
    uint16_t enum_off;       /* start index into enum_name_offsets[] */
    uint8_t  enum_count;     /* number of enum names */
    uint8_t  type;           /* vc_type_t */
    uint8_t  vtx;            /* vc_tx_t */
    uint8_t  decimals;       /* editor decimals (DOUBLE) */
    uint8_t  flags;          /* VC_FLAG_* */
} vc_param_t;

/* One entry inside a subgroup's parameter list. param_idx == -1 marks a
 * "::sep::Label" separator, in which case sep_label_off names the section. */
typedef struct {
    int16_t  param_idx;
    uint32_t sep_label_off;
} vc_group_entry_t;

typedef struct {
    uint32_t name_off;
    uint16_t first_entry;    /* into group_entries[] */
    uint16_t entry_count;
} vc_subgroup_t;

typedef struct {
    uint32_t name_off;
    uint16_t first_sub;      /* into subgroups[] */
    uint16_t sub_count;
} vc_group_t;

/* A full config table for one (firmware version, kind). */
typedef struct {
    const char             *str_pool;
    const uint32_t         *enum_name_offsets;
    const vc_param_t       *params;
    uint16_t                param_count;
    const uint16_t         *ser_order;      /* indices into params[], wire order */
    uint16_t                ser_count;
    const vc_group_t       *groups;
    uint16_t                group_count;
    const vc_subgroup_t    *subgroups;
    uint16_t                subgroup_count;
    const vc_group_entry_t *group_entries;
    uint16_t                group_entry_count;
    uint32_t                expected_signature;  /* baked crc32c, for self-check */
    uint8_t                 fw_major;
    uint8_t                 fw_minor;
    uint8_t                 kind;                /* 0 mcconf, 1 appconf */
} vc_table_t;

/* Version index — one entry per baked firmware version. */
typedef struct {
    uint8_t major;
    uint8_t minor;
    const vc_table_t *mcconf;
    const vc_table_t *appconf;
} vc_version_t;

extern const vc_version_t g_vc_versions[];
extern const size_t       g_vc_version_count;

/* Convenience: a pool string by offset (offset 0 -> ""). */
static inline const char *vc_str(const vc_table_t *t, uint32_t off)
{
    return t->str_pool + off;
}

#ifdef __cplusplus
}
#endif
