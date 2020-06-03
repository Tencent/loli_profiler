#pragma once

// #include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void *fake_dlopen(const char *libpath, int flags);
void *fake_dlsym(void *handle, const char *name);

#ifdef __cplusplus
}
#endif // __cplusplus