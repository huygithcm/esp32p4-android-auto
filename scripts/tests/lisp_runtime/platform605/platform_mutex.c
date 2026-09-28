#include "platform_mutex.h"
bool mutex_init(mutex_t *m) { InitializeCriticalSection(m); return true; }
void mutex_lock(mutex_t *m) { EnterCriticalSection(m); }
void mutex_unlock(mutex_t *m) { LeaveCriticalSection(m); }
