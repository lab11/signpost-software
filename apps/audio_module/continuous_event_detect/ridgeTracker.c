/*
 * Ridge tracker
 * 
 * Long Le <longle2718@gmail.com>
 * University of Illinois
 */

#include <stdlib.h>
#include <stdbool.h>

#include "ridgeTracker.h"
#include "_kiss_fft_guts.h"
#include "common.h"
#include "log2fix/log2fix.h"
#include "dynArray.h"

#define FOFF        (2)
// SAMP_MAX is fixed-point 1
#define BTLEN       (10) // int(btTime/tInc)
#define ALP         ((kiss_fft_scalar)(0.9*SAMP_MAX)) // exp(-tInc/btTime)
#define SUPTHRESH   ((kiss_fft_scalar)(0.3*SAMP_MAX))
#define NADA        ((kiss_fft_scalar)(0.01*SAMP_MAX))
#define EPSILON     ((kiss_fft_scalar)(0.001*SAMP_MAX)) //noise adaptive param
kiss_fft_scalar ind[FRE_LEN];
kiss_fft_scalar noiseFloor[FRE_LEN];
kiss_fft_scalar snrAcc[FRE_LEN];
kiss_fft_scalar freAcc[FRE_LEN];

#define PROBTARGET  ((kiss_fft_scalar)(0.5*SAMP_MAX))
#define MUP         ((kiss_fft_scalar)(0.99*SAMP_MAX))
#define MUT         ((kiss_fft_scalar)(0.99*SAMP_MAX))
int curTime;
kiss_fft_scalar adaThresh;
kiss_fft_scalar probEst;

bool ridgeTracker_isReady;
Array ridgeTracker_out;

void ridgeTracker_init(void){
    for (size_t f=0; f<FRE_LEN; f++){
        ind[f] = BTLEN;
        noiseFloor[f] = (kiss_fft_scalar)(0.002*SAMP_MAX);
        snrAcc[f] = 0;
        freAcc[f] = f;
    }

    curTime = -1;
    adaThresh = ((kiss_fft_scalar)(0.05*SAMP_MAX));
    probEst = PROBTARGET;
    
    ridgeTracker_isReady = false;
    initArray(&ridgeTracker_out,5);
    debug_printf("ridgeTracker_init() completed\n");
}

void ridgeTracker_reset(void){
    curTime = -1;
    debug_printf("Timer reset\n");

    ridgeTracker_isReady = false;
    freeArray(&ridgeTracker_out);
}

void ridgeTracker_update(kiss_fft_scalar* spec, kiss_fft_scalar* snrOut){
    kiss_fft_scalar snr,fLow,fHigh,wWin,val,maxVal,maxIdx;
    static kiss_fft_scalar snrAccLast[FRE_LEN];

    memcpy(snrAccLast, snrAcc, FRE_LEN*sizeof(kiss_fft_scalar));
    for (size_t f=0; f<FRE_LEN; f++){
        if (spec[f] > noiseFloor[f]){
            ind[f] -= 1;
            if (ind[f] < 0){
                noiseFloor[f] += S_MUL(noiseFloor[f],NADA);
            }else{
                noiseFloor[f] += S_MUL(noiseFloor[f],NADA/2);
            }
        }else{
            noiseFloor[f] = MAX(EPSILON, noiseFloor[f]-S_MUL(noiseFloor[f],NADA));
            ind[f] = BTLEN;
        }

        // snr update
        uint32_t r = spec[f]/noiseFloor[f];
        if (r >= 10){
            snr = SAMP_MAX;
        }else if (r > 0){
            // see log2fix/main.c
            snr = log10fix(r*(1<<FRACBITS),FRACBITS);
        }else{
            snr = 0;
        }
        fLow = MAX(0,f-FOFF);
        fHigh = MIN(FRE_LEN-1, f+FOFF);
        maxVal = -1; 
        maxIdx = -1;
        for (size_t l=fLow; l<=fHigh; l++){
            wWin = SAMP_MAX - S_MUL((kiss_fft_scalar)(0.05*SAMP_MAX),ABS(l-f))/FOFF;
            val = S_MUL(ALP,snrAccLast[f]) + S_MUL(wWin,snr); 
            if (val > maxVal){
                maxVal = val;
                maxIdx = l;
            }
        }
        snrAcc[f] = maxVal;
        freAcc[f] = maxIdx;
    }

    // max-pooling
    memcpy(snrAccLast, snrAcc, FRE_LEN*sizeof(kiss_fft_scalar));
    for (size_t f=0; f<FRE_LEN; f++){
        fLow = MAX(0,f-FOFF);
        fHigh = MIN(FRE_LEN-1, f+FOFF);
        maxVal = -1;
        maxIdx = -1;
        for (size_t l=fLow; l<=fHigh; l++){
            if (snrAccLast[l] > maxVal){
                maxVal = snrAccLast[l];
                maxIdx = l;
            }
        }
        if (maxIdx == f){
            snrAcc[f] = maxVal;
        }else{
            snrAcc[f] = 0;
        }
    }

    // per-bin suppressed stats
    memset(snrOut, 0, FRE_LEN*sizeof(kiss_fft_scalar));
    bool binActive = false;
    for (size_t f=0; f<FRE_LEN; f++){
        if (snrAcc[f] > SUPTHRESH){
            binActive = true;
            led_toggle(1);
            led_toggle(1);
            snrOut[f] = snrAcc[f] - SUPTHRESH;

            if (curTime == -1){
                debug_printf("\nStart timer\n");
                curTime = BTLEN;
            }
            insertArray(&ridgeTracker_out,snrOut[f],f,(size_t)curTime);
        }
    }

    // output if applicable
    if (curTime != -1){
        if (!binActive){
            debug_printf("Potential output\n");
            // longer time-granuality approximation
            if (getMaxDurArray(ridgeTracker_out) >= 0){
                debug_printf("Sufficiently long temporal approximation\n");
                // adaptive thresholding
                debug_printf("avgSNR = %d\n", getAvgSNRArray(ridgeTracker_out));
                if (getAvgSNRArray(ridgeTracker_out) >= adaThresh){
                    ridgeTracker_isReady = true;
                    probEst = S_MUL(MUP,probEst) + (SAMP_MAX-MUP);
                } else{
                    ridgeTracker_reset();
                    probEst = S_MUL(MUP,probEst);
                }
            } else{
                ridgeTracker_reset();
            }
            adaThresh = adaThresh + S_MUL(SAMP_MAX-MUT,probEst-PROBTARGET);

        } else{
            curTime += 1;
        }
    }
}

void ridgeTracker_destroy(void){
    // nothing to free anymore
    debug_printf("ridgeTracker_destroy() completed\n");
}
