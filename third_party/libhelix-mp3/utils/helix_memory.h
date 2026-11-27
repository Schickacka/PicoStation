#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *pico_audio_helix_malloc(size_t sz);
void  pico_audio_helix_free(void *ptr);

#ifdef __cplusplus
}
#endif

#define helix_malloc(sz)     pico_audio_helix_malloc(sz)
#define helix_calloc(n, sz)  pico_audio_helix_malloc((n) * (sz))
#define helix_free(ptr)      pico_audio_helix_free(ptr)
