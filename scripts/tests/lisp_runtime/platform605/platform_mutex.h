/* Host-only Windows implementation of the pinned 6.05 platform mutex API.
 * No VM semantics or ESC extensions are added by this adapter. */
#ifndef PLATFORM_MUTEX_H_
#define PLATFORM_MUTEX_H_
#include <windows.h>
#include <stdbool.h>
typedef CRITICAL_SECTION mutex_t;
bool mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
#endif
