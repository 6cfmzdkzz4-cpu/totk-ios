/* Exact-build live display adapter; installation never changes executable pages. */
#pragma once
#include "runtime.h"

void runtime_live_display_install(Module *module);
uint64_t runtime_live_display_calculate_calls(void);

/* Copy the currently bound owner identifier for private diagnostics. */
void runtime_live_display_owner_name(uintptr_t layout,uintptr_t root,char name[96]);

/* Private live-display request and immutable completed-interval receipt. */
typedef struct {
    uint64_t frame,generation,requested_generation,hud_adjustments,hud_misses;
    double aspect,width,height,scale,safe_insets[4];
    int paired,valid,fallback,orientation;
} TotkLiveSource;
int runtime_live_display_request(double width,double height,double scale,const double insets[4]);
int runtime_live_display_request_scene(double width,double height,double scale,const double insets[4],unsigned orientation);
/* Explicit profile fallback keeps real scene geometry while rendering the
   prepared4:3 pair. Invalid/transient geometry still preserves the last request. */
int runtime_live_display_request_scene_mode(double width,double height,double scale,const double insets[4],unsigned orientation,int fallback);
TotkLiveSource runtime_live_display_finish_interval(uint64_t frame);
int runtime_live_display_enabled(void);
