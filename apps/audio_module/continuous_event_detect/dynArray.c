#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "dynArray.h"
#include "common.h"
#include "_kiss_fft_guts.h"

#define BUF_SIZE 1600

void initArray(Array *a, size_t initialSize) {
    static kiss_fft_scalar SNR_BUF[BUF_SIZE];
    static size_t FI_BUF[BUF_SIZE];
    static size_t TI_BUF[BUF_SIZE];
    static bool used = false;

    //Note: We are ignoring the initial size here and using a static maximum
    // size instead in order to avoid malloc
    a->maxSize = BUF_SIZE;
    a->used = 0;

    a->size = a->maxSize;
    debug_printf("Init array size = %zu\n",a->size);
    if (!used) {
        a->SNR = SNR_BUF;
        a->FI = FI_BUF;
        a->TI = TI_BUF;
        used = true;
    } else {
        printf("Trying to init second array!\n");
    }
}

void insertArray(Array *a, kiss_fft_scalar snr, size_t fi, size_t ti) {

    // we no longer double the array, but rather have a fixed size in order to
    // avoid malloc. This means we could run out of space. If we do, just throw
    // out all the old data and start over
    if (a->used >= a->size) {
        printf("Ran out of space!\n");
        a->used = 0;
        return;
    }

    a->SNR[a->used] = snr;
    a->FI[a->used] = fi;
    a->TI[a->used] = ti;
    a->used++;
}

void freeArray(Array *a) {
    // nothing needs to be freed anymore, just reset the used index
    a->used = 0;
}
size_t getMaxDurArray(Array a){
    if (a.used == 0)
        return 0;

    size_t minTI = a.TI[0];
    size_t maxTI = a.TI[a.used-1];
    return maxTI-minTI;
}

kiss_fft_scalar getAvgSNRArray(Array a){
    if (a.used == 0)
        return 0;

    SAMPPROD sum = 0;
    for (size_t k=0; k < a.used; k++){
        sum += a.SNR[k];
    }
    return (kiss_fft_scalar)(sum/a.used);
}
