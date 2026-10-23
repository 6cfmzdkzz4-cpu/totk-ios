/* Proportional live-fit and temporal epochs, without guest bytes or a device. */
#include "../games/totk/runtime/totk_scene_geometry.h"
#include <assert.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while(0)

int main(void) {
    /* Installed Duo profile dimensions are fixtures, not runtime device matching.
       Test both orientations; UIKit's actual scene supplies production bounds. */
    const double scenes[][3]={{1398,2034,3},{2034,1398,3},{2007,2853,3},{2853,2007,3},
        {2868,1320,3},{2752,2064,2},{430,932,1},{320,240,1},{7680,4320,3}};
    const double aspects[]={4.0/3.0,239.0/110.0,16.0/9.0};
    CHECK(totk_scene_narrow_supported(951,669));CHECK(totk_scene_narrow_supported(678,466));
    CHECK(totk_scene_narrow_supported(1024,1024));CHECK(totk_scene_narrow_supported(1920,1080));
    CHECK(!totk_scene_narrow_supported(2868,1320));CHECK(!totk_scene_narrow_supported(466,678));
    CHECK(!totk_scene_narrow_supported(NAN,669));
    for(unsigned i=0;i<sizeof scenes/sizeof *scenes;i++)for(unsigned a=0;a<3;a++){
        double w=scenes[i][0]/scenes[i][2],h=scenes[i][1]/scenes[i][2],s=scenes[i][2];
        dm_display_geometry live=totk_scene_picture(w,h,s,aspects[a],1);
        CHECK(live.valid && fabs(live.picture.width-w)<1e-9 && fabs(live.picture.height-h)<1e-9);
        dm_display_geometry legacy=totk_scene_picture(w,h,s,aspects[a],0);
        CHECK(legacy.valid && fabs(legacy.picture.width/legacy.picture.height-aspects[a])<1e-9);
        dm_display_geometry g=dm_display_make(w,h,s,aspects[a]);
        CHECK(g.valid);CHECK(g.picture.x>=0 && g.picture.y>=0);
        CHECK(g.picture.width<=w && g.picture.height<=h);
        CHECK(fabs(g.picture.width/g.picture.height-aspects[a])<1e-12);
        CHECK(fabs(g.picture.x*2+g.picture.width-w)<1e-9);
        CHECK(fabs(g.picture.y*2+g.picture.height-h)<1e-9);
        dm_display_geometry px=totk_scene_drawable(g.picture.width,g.picture.height,s,1080);
        CHECK(px.valid && px.pixel_width>0 && px.pixel_height>0);
        CHECK(px.pixel_height<=1080 && px.pixel_height<=g.pixel_height);
        CHECK(px.pixel_width<=g.pixel_width);
        CHECK(fabs((double)px.pixel_width/px.pixel_height-aspects[a])<1.0/px.pixel_height);
        dm_display_geometry native=totk_scene_drawable(g.picture.width,g.picture.height,s,0);
        CHECK(native.pixel_width==g.pixel_width && native.pixel_height==g.pixel_height);
        CHECK(!dm_display_drawable_changed(px,px));
    }
    const double invalid[][3]={{0,430,3},{932,0,3},{-1,430,3},{932,430,0},
        {NAN,430,3},{932,INFINITY,3},{932,430,NAN},{20000,430,1}};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++){
        dm_display_geometry g=totk_scene_drawable(invalid[i][0],invalid[i][1],invalid[i][2],1080);
        CHECK(!g.valid);
        CHECK(!dm_display_drawable_changed((dm_display_geometry){0},g));
    }
    totk_geometry_epoch epoch={0};
    CHECK(totk_geometry_now(&epoch)==0);CHECK(!totk_geometry_consume(&epoch,0));
    totk_geometry_changed(&epoch);
    CHECK(totk_geometry_now(&epoch)==1);CHECK(totk_geometry_consume(&epoch,1));
    CHECK(!totk_geometry_consume(&epoch,1));
    CHECK(!totk_geometry_generated_current(&epoch,0));
    CHECK(totk_geometry_generated_current(&epoch,1));
    totk_geometry_changed(&epoch);totk_geometry_changed(&epoch);
    CHECK(totk_geometry_consume(&epoch,3));CHECK(!totk_geometry_consume(&epoch,3));
    CHECK(!totk_geometry_generated_current(&epoch,1));
    /* Repeated fold/unfold requests coalesce at worker boundaries; old FG never
       becomes current again. Native guest surfaces are not retired by epochs. */
    for(unsigned i=0;i<10000;i++){
        uint64_t old=totk_geometry_now(&epoch);totk_geometry_changed(&epoch);
        CHECK(!totk_geometry_generated_current(&epoch,old));
        CHECK(totk_geometry_consume(&epoch,old+1));
        CHECK(!totk_geometry_consume(&epoch,old+1));
    }
    printf("%u live display geometry checks passed\n",checks);
    return 0;
}
