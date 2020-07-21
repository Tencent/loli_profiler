#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdlib.h>

bool loli_server_started();
int loli_server_start(int port);
void loli_server_send(const char* data, unsigned int size);
void loli_server_shutdown();

#ifdef __cplusplus
}
#endif // __cplusplus