// Immutable completed TOTK intervals, shared by native presentation and MetalFX.
#pragma once
#include "../../../../runtime/totk_live_display.h"
#include <math.h>
#include <string.h>
static inline int totk_source_valid(TotkLiveSource source) {
    if(!source.valid || !isfinite(source.aspect) || source.aspect<=0 || source.aspect>8 ||
       !isfinite(source.width) || !isfinite(source.height) || !isfinite(source.scale) ||
       source.width<0 || source.height<0 || source.scale<0)return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(source.safe_insets[i]) || source.safe_insets[i]<0)return 0;
    return 1;
}
static inline int totk_source_geometry_equal(TotkLiveSource a,TotkLiveSource b) {
    return totk_source_valid(a) && totk_source_valid(b) && a.generation==b.generation &&
        a.paired==b.paired && a.fallback==b.fallback && a.aspect==b.aspect && a.width==b.width && a.height==b.height && a.scale==b.scale &&
        !memcmp(a.safe_insets,b.safe_insets,sizeof a.safe_insets);
}
