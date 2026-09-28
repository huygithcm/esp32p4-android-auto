/* Host integration fixture, following the initialization sequence in pinned
 * bldc 7.00 lispBM/lispBM/tests/test_lisp_code_cps.c (GPL-3.0-or-later).
 * The interpreter and Windows platform implementations are unmodified upstream.
 * No ESC driver is linked. Hardware/API behavior is supplied by Lisp fixtures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "lispbm.h"
#if !HOST_VESC_605
#include "lbm_image.h"
#include "platform_timestamp.h"
#endif
#include "extensions/array_extensions.h"
#include "extensions/math_extensions.h"
#include "extensions/string_extensions.h"
#include "extensions/runtime_extensions.h"
#if HOST_VESC_605
#include "firmware_macros.h"
bool lispif_vesc_dynamic_loader(const char *str, const char **code);
static lbm_const_heap_t constants;
static uint32_t lbm_timestamp(void) {
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (uint32_t)((counter.QuadPart * 1000000) / frequency.QuadPart);
}
#else
#include "extensions/lbm_dyn_lib.h"
#endif

#ifndef HOST_HEAP_CELLS
#define HOST_HEAP_CELLS 4096
#endif
static lbm_cons_t heap[HOST_HEAP_CELLS];
/* 6.05 lispif.c specifies 18 KiB but its bundled header has no 18K alias. */
#define HOST_MEMORY_SIZE LBM_MEMORY_SIZE_64BYTES_TIMES_X(288)
#define HOST_BITMAP_SIZE LBM_MEMORY_BITMAP_SIZE(288)
static lbm_uint memory[HOST_MEMORY_SIZE];
static lbm_uint bitmap[HOST_BITMAP_SIZE];
static lbm_extension_t extensions[256];
static uint32_t image_storage[64 * 1024];
static volatile LONG done;
static volatile LONG failed;
static int checks;

#if HOST_VESC_605
static bool image_write(lbm_uint ix, lbm_uint word) {
#else
static bool image_write(uint32_t word, int32_t ix, bool constant) {
    (void)constant;
#endif
    if (ix < 0 || (size_t)ix >= sizeof(image_storage) / sizeof(image_storage[0])) return false;
    if (image_storage[ix] != 0xffffffff && image_storage[ix] != word) return false;
    image_storage[ix] = word;
    return true;
}
static void host_sleep(uint32_t us) { Sleep(us ? (us + 999) / 1000 : 0); }
static DWORD WINAPI run_eval(LPVOID unused) { (void)unused; lbm_run_eval(); return 0; }
#if !HOST_VESC_605
static DWORD WINAPI run_clock(LPVOID unused) { lbm_timestamp_cacher(unused); return 0; }
#endif
static void critical_error(void) { fputs("FAIL critical interpreter error\n", stderr); exit(2); }
static void context_done(eval_context_t *ctx) {
    char result[256];
    lbm_print_value(result, sizeof(result), ctx->r);
    printf("CONTEXT %d finished: %s\n", (int)ctx->id, result);
}
LBM_EXTENSION(test_check, args, argn) {
    if (argn != 2) return ENC_SYM_EERROR;
    char name[256];
    lbm_print_value(name, sizeof(name), args[1]);
    bool ok = args[0] == ENC_SYM_TRUE;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    checks++;
    if (!ok) InterlockedExchange(&failed, 1);
    return ENC_SYM_TRUE;
}
LBM_EXTENSION(test_finish, args, argn) {
    (void)args; (void)argn;
    InterlockedExchange(&done, 1);
    return ENC_SYM_TRUE;
}
LBM_EXTENSION(host_time, args, argn) {
    (void)args; (void)argn;
    return lbm_enc_u32(lbm_timestamp());
}
LBM_EXTENSION(host_elapsed, args, argn) {
    if (argn != 1 || !lbm_is_number(args[0])) return ENC_SYM_EERROR;
    return lbm_enc_float((float)(uint32_t)(lbm_timestamp() - lbm_dec_as_u32(args[0])) / 1000000.0f);
}
LBM_EXTENSION(host_print, args, argn) {
    for (lbm_uint i = 0; i < argn; i++) {
        char output[256];
        lbm_print_value(output, sizeof(output), args[i]);
        printf("%s%s", i ? " " : "", output);
    }
    putchar('\n');
    return ENC_SYM_TRUE;
}
LBM_EXTENSION(host_event_pid, args, argn) {
    (void)args; (void)argn;
    return lbm_enc_i(lbm_get_event_handler_pid());
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 2;
    fseek(input, 0, SEEK_END);
    long size = ftell(input);
    rewind(input);
    char *code = calloc((size_t)size + 1, 1);
    if (!code || fread(code, 1, (size_t)size, input) != (size_t)size) return 2;
    fclose(input);
    if (!lbm_init(heap, HOST_HEAP_CELLS, memory, HOST_MEMORY_SIZE,
                  bitmap, HOST_BITMAP_SIZE, 160, 256, extensions, 256)) return 2;
    memset(image_storage, 0xff, sizeof(image_storage));
#if HOST_VESC_605
    if (!lbm_const_heap_init(image_write, &constants, image_storage,
                             sizeof(image_storage) / sizeof(lbm_uint))) return 2;
    lbm_set_timestamp_us_callback(lbm_timestamp);
#else
    lbm_image_init(image_storage, sizeof(image_storage) / sizeof(lbm_uint), image_write);
    lbm_image_create("host-runtime-audit");
    if (!lbm_image_boot()) return 2;
    lbm_add_eval_symbols();
#endif
    if (!lbm_eval_init_events(20)) return 2;
    lbm_array_extensions_init();
    lbm_math_extensions_init();
    lbm_string_extensions_init();
#if HOST_VESC_605
    lbm_runtime_extensions_init(false);
    firmware_macros_init();
    lbm_set_dynamic_load_callback(lispif_vesc_dynamic_loader);
#else
    lbm_runtime_extensions_init();
    lbm_dyn_lib_init();
    lbm_set_dynamic_load_callback(lbm_dyn_lib_find);
#endif
    lbm_set_usleep_callback(host_sleep);
    lbm_set_printf_callback(printf);
    lbm_set_critical_error_callback(critical_error);
    lbm_set_ctx_done_callback(context_done);
    lbm_set_verbose(false);
    lbm_add_extension("test-check", test_check);
    lbm_add_extension("test-finish", test_finish);
    lbm_add_extension("systime", host_time);
    lbm_add_extension("secs-since", host_elapsed);
    lbm_add_extension("print", host_print);
    lbm_add_extension("test-event-pid", host_event_pid);
#if !HOST_VESC_605
    CreateThread(NULL, 0, run_clock, NULL, 0, NULL);
#endif
    CreateThread(NULL, 0, run_eval, NULL, 0, NULL);
    lbm_pause_eval_with_gc(20);
    while (lbm_get_eval_state() != EVAL_CPS_STATE_PAUSED) Sleep(1);
    lbm_char_channel_t channel;
    lbm_string_channel_state_t channel_state;
    lbm_create_string_char_channel(&channel_state, &channel, code);
    if (lbm_load_and_eval_program_incremental(&channel, NULL) == -1) return 2;
    lbm_continue_eval();
    for (int i = 0; i < 20000 && !done; i++) Sleep(1);
    if (!done) { fputs("FAIL runtime timed out or a required thread died\n", stderr); return 1; }
    printf("%d actual LispBM checks; %s; ESC APIs are host fixtures\n",
           checks, failed ? "FAILED" : "PASSED");
    return failed || checks == 0;
}
