/* Concurrent last-slot and deduplication regression for the production tables. */
#include "totk_live_reservations.h"
#include <assert.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { THREADS=32, ATTEMPTS=512 };
static struct {uint64_t before;TotkLiveReservations table;uint64_t after;} state;
static _Atomic unsigned ready,go,accepted;
typedef struct {unsigned index;int rejection,duplicate;} Work;
static void *worker(void *pointer) {
    Work *work=pointer;
    atomic_fetch_add_explicit(&ready,1,memory_order_release);
    while(!atomic_load_explicit(&go,memory_order_acquire))sched_yield();
    for(unsigned i=0;i<ATTEMPTS;i++) {
        uint64_t key=work->duplicate?777:100000+work->index*ATTEMPTS+i;
        if(totk_live_reserve(&state.table,key,work->rejection))atomic_fetch_add(&accepted,1);
    }
    return NULL;
}
static unsigned run(int rejection,int duplicate,unsigned fill) {
    memset(&state,0,sizeof state);state.before=UINT64_C(0xced00aced00ace);state.after=UINT64_C(0xfed00afed00afe);
    assert(!pthread_mutex_init(&state.table.lock,NULL));
    for(unsigned i=0;i<fill;i++)assert(totk_live_reserve(&state.table,100+i,rejection));
    atomic_store(&ready,0);atomic_store(&go,0);atomic_store(&accepted,0);
    pthread_t threads[THREADS];Work work[THREADS];
    for(unsigned i=0;i<THREADS;i++){work[i]=(Work){i,rejection,duplicate};assert(!pthread_create(&threads[i],NULL,worker,&work[i]));}
    while(atomic_load_explicit(&ready,memory_order_acquire)<THREADS)sched_yield();
    atomic_store_explicit(&go,1,memory_order_release);
    for(unsigned i=0;i<THREADS;i++)assert(!pthread_join(threads[i],NULL));
    unsigned added=atomic_load(&accepted);
    unsigned count=rejection?state.table.rejection_count:state.table.survey_count;
    assert(count==fill+added);
    assert(state.table.survey_count<=TOTK_LIVE_SURVEY_CAP && state.table.rejection_count<=TOTK_LIVE_REJECT_CAP);
    assert(state.before==UINT64_C(0xced00aced00ace) && state.after==UINT64_C(0xfed00afed00afe));
    assert(!pthread_mutex_destroy(&state.table.lock));return added;
}
int main(void) {
    unsigned rounds=0;
    for(unsigned i=0;i<24;i++) {
        assert(run(0,0,95)==1);assert(run(1,0,15)==1);
        assert(run(0,1,0)==1);assert(run(1,1,0)==1);
        assert(run(0,0,96)==0);assert(run(1,0,16)==0);rounds+=6;
    }
    printf("{\"status\":\"pass\",\"rounds\":%u,\"threads_each\":%u,\"attempts_each\":%u}\n",rounds,THREADS,ATTEMPTS);
}
