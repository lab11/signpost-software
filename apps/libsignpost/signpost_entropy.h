#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int signpost_entropy_init (void);
int signpost_entropy_rand(uint8_t* buf, size_t len, size_t num);

#ifdef __cplusplus
}
#endif
