#include <stdbool.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

#include "signpost_entropy.h"
#include "port_signpost.h"

mbedtls_ctr_drbg_context ctr_drbg_context;
static mbedtls_entropy_context entropy_context;
static uint8_t drbg_data[32];

static int rng_wrapper(void* data __attribute__ ((unused)), uint8_t* out, size_t len, size_t* olen) {
    int num = port_rng_sync(out, len, len);
    if (num < 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = num;
    return 0;
}

int signpost_entropy_init (void) {
    int rc;
    // init rng
    rc = port_rng_init();
    if (rc < 0) return rc;
    // random start seed
    rc = port_rng_sync(drbg_data, 32, 32);
    if (rc < 0) return rc;

    // init entropy and prng
    mbedtls_entropy_init(&entropy_context);
    rc = mbedtls_entropy_add_source(&entropy_context, rng_wrapper, NULL, 48, true);
    if (rc < 0) return rc;
    mbedtls_ctr_drbg_free(&ctr_drbg_context);
    mbedtls_ctr_drbg_init(&ctr_drbg_context);
    rc = mbedtls_ctr_drbg_seed(&ctr_drbg_context, mbedtls_entropy_func, &entropy_context, drbg_data, 32);
    if (rc < 0) return rc;
    mbedtls_ctr_drbg_set_prediction_resistance(&ctr_drbg_context, MBEDTLS_CTR_DRBG_PR_ON);
    return 0;
}

int signpost_entropy_rand(uint8_t* buf, size_t len, size_t num) {
    size_t bytes_to_request;
    if (len < num) {
        bytes_to_request = len;
    } else {
        bytes_to_request = num;
    }
    return mbedtls_ctr_drbg_random(&ctr_drbg_context, buf, bytes_to_request);
}
