/* Signed TOTK display profiles, actual scene sizing and presentation epochs. */
#pragma once
#include "dm_display_geometry.h"
#include <stdatomic.h>

typedef struct { _Atomic uint64_t requested; uint64_t consumed; } totk_geometry_epoch;
/* The shipping narrow profile has the verified4:3 preparation. Keep its
   landscape qualification separate from the static239:110 phone preparation. */
static inline int totk_scene_narrow_supported(double width,double height) {
    return isfinite(width) && isfinite(height) && width>0 && height>0 && width/height>=1 && width/height<=16.0/9.0;
}
static inline dm_display_geometry totk_scene_picture(double width,double height,double scale,double signed_aspect,int live_narrow) {
    return dm_display_make(width,height,scale,live_narrow && width>0 && height>0?width/height:signed_aspect);
}
static inline uint64_t totk_geometry_now(const totk_geometry_epoch *g) {
    return atomic_load_explicit(&g->requested,memory_order_acquire);
}
static inline void totk_geometry_changed(totk_geometry_epoch *g) {
    atomic_fetch_add_explicit(&g->requested,1,memory_order_release);
}
/* Only the frame-processing worker owns consumed. UIKit only advances requested. */
static inline int totk_geometry_consume(totk_geometry_epoch *g,uint64_t epoch) {
    if(g->consumed==epoch)return 0;
    g->consumed=epoch;return 1;
}
static inline int totk_geometry_generated_current(const totk_geometry_epoch *g,uint64_t epoch) {
    return epoch==totk_geometry_now(g);
}
/* The view is already fitted to the verified camera/HUD ratio. Keep the user's
   content-height budget independent of native display density or fold posture. */
static inline dm_display_geometry totk_scene_drawable(double width,double height,double scale,long maximum_height) {
    if(!isfinite(width) || !isfinite(height) || width<=0 || height<=0)return (dm_display_geometry){0};
    dm_display_geometry g=dm_display_make(width,height,scale,width/height);
    if(!g.valid)return g;
    if(maximum_height>0 && (double)maximum_height<g.pixel_height){
        double f=(double)maximum_height/g.pixel_height;
        g.pixel_width=(uint32_t)fmax(1,floor(g.pixel_width*f+.5));
        g.pixel_height=(uint32_t)maximum_height;
    }
    return g;
}
