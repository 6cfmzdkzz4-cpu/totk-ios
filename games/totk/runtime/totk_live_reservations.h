/* Bounded diagnostic reservations shared by parallel UI Calculate callbacks.
   The lock covers bookkeeping only; callers run guest code outside it. */
#pragma once
#include <pthread.h>
#include <stdint.h>

enum { TOTK_LIVE_SURVEY_CAP=96, TOTK_LIVE_REJECT_CAP=16 };
typedef struct {
    pthread_mutex_t lock;
    unsigned survey_count,rejection_count;
    uint64_t surveyed[TOTK_LIVE_SURVEY_CAP];
    uint64_t rejected[TOTK_LIVE_REJECT_CAP];
} TotkLiveReservations;
#define TOTK_LIVE_RESERVATIONS_INITIALIZER { .lock=PTHREAD_MUTEX_INITIALIZER }

static inline int totk_live_reserve(TotkLiveReservations *table,uint64_t key,int rejection) {
    pthread_mutex_lock(&table->lock);
    unsigned *count=rejection?&table->rejection_count:&table->survey_count;
    unsigned capacity=rejection?TOTK_LIVE_REJECT_CAP:TOTK_LIVE_SURVEY_CAP;
    uint64_t *keys=rejection?table->rejected:table->surveyed;
    int result=0;
    if(*count<capacity) {
        unsigned index=0;while(index<*count && keys[index]!=key)index++;
        if(index==*count){keys[(*count)++]=key;result=1;}
    }
    pthread_mutex_unlock(&table->lock);
    return result;
}
static inline int totk_live_has_room(TotkLiveReservations *table) {
    pthread_mutex_lock(&table->lock);
    int result=table->survey_count<TOTK_LIVE_SURVEY_CAP || table->rejection_count<TOTK_LIVE_REJECT_CAP;
    pthread_mutex_unlock(&table->lock);
    return result;
}
