/* Stable 4:3 picture, 16:9 cutscene fit, and honest frame-cap pacing. */
#define _POSIX_C_SOURCE 200112L
#include "../games/totk/runtime/totk_stable_aspect.h"
#include "../games/totk/runtime/totk_frame_cap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int dm_fps_setting(void);

static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)

static void expect_picture(double width, double height) {
    dm_display_geometry picture = totk_stable_scene_picture(width, height, 3, 0, 0, 0, 0);
    dm_display_geometry again = totk_stable_scene_picture(width, height, 3, 0, 0, 0, 0);
    CHECK(picture.valid && picture.pixel_width > 0 && picture.pixel_height > 0);
    CHECK(fabs(picture.picture.width / picture.picture.height - TOTK_STABLE_CONTENT_ASPECT) < 1e-9);
    CHECK(picture.picture.x > 1 && fabs(picture.picture.y) < 1e-9);
    CHECK(fabs(picture.picture.x * 2 + picture.picture.width - width) < 1e-6);
    CHECK(fabs(picture.picture.height - height) < 1e-6);
    CHECK(picture.picture.x == again.picture.x && picture.picture.width == again.picture.width);
    CHECK(fabs((double)picture.pixel_width / picture.pixel_height - TOTK_STABLE_CONTENT_ASPECT) < 1.0 / picture.pixel_height);
    dm_display_rect movie = totk_cutscene_in_picture(picture.picture);
    CHECK(movie.width > 0 && movie.height > 0);
    CHECK(fabs(movie.width / movie.height - TOTK_CUTSCENE_ASPECT) < 1e-9);
    CHECK(movie.x >= picture.picture.x - 1e-6 && movie.y >= picture.picture.y - 1e-6);
    CHECK(movie.x + movie.width <= picture.picture.x + picture.picture.width + 1e-6);
    CHECK(movie.y + movie.height <= picture.picture.y + picture.picture.height + 1e-6);
    CHECK(fabs(movie.width - picture.picture.width) < 1e-6);
    CHECK(movie.height < picture.picture.height - 1);
    CHECK(fabs(picture.picture.x - again.picture.x) < 1e-9);
}

int main(void) {
    unsetenv("TOTK_FPS_CAP");
    /* Public iPhone Duo landscape point sizes: outer 678x466, inner 890x626.
       These are fixtures for the fit, not a runtime device-id check. */
    expect_picture(678, 466);
    expect_picture(890, 626);
    dm_display_geometry safe = totk_stable_scene_picture(890, 626, 3, 20, 0, 20, 0);
    CHECK(safe.valid && safe.picture.x >= 20);
    CHECK(fabs(safe.picture.width / safe.picture.height - TOTK_STABLE_CONTENT_ASPECT) < 1e-9);
    CHECK(!totk_stable_scene_picture(100, 80, 2, 60, 0, 60, 0).valid);
    CHECK(!totk_stable_scene_picture(0, 466, 3, 0, 0, 0, 0).valid);
    CHECK(!totk_frame_cap_get());
    CHECK(dm_fps_setting() == 60);
    CHECK(totk_frame_cap_normalize(45) == 0 && totk_frame_cap_normalize(0) == 0);
    CHECK(totk_frame_cap_normalize(90) == 0 && totk_frame_cap_normalize(120) == 0);
    CHECK(totk_frame_cap_unavailable(90) && totk_frame_cap_unavailable(120));
    CHECK(!totk_frame_cap_unavailable(30) && !totk_frame_cap_unavailable(60));
    CHECK(totk_frame_cap_applied(30) == 30 && totk_frame_cap_applied(60) == 60);
    CHECK(totk_frame_cap_applied(90) == 0 && totk_frame_cap_applied(120) == 0 && totk_frame_cap_applied(0) == 0);
    CHECK(totk_frame_cap_pace(0, 30) == 1000000000ull / 30);
    CHECK(totk_frame_cap_pace(1000000000ull / 120, 30) == 1000000000ull / 30);
    CHECK(totk_frame_cap_pace(1000000000ull / 120, 60) == 1000000000ull / 60);
    CHECK(totk_frame_cap_pace(1000000000ull / 20, 60) == 1000000000ull / 20);
    CHECK(totk_frame_cap_pace(1000000000ull / 120, 90) == 1000000000ull / 120);
    totk_frame_cap_set(90);
    totk_frame_cap_set(120);
    CHECK(!totk_frame_cap_get());
    setenv("TOTK_FPS_CAP", "90", 1);
    CHECK(totk_frame_cap_get() == 30 && totk_frame_cap_applied_now() == 30);
    unsetenv("TOTK_FPS_CAP");
    totk_frame_cap_set(60);
    CHECK(totk_frame_cap_get() == 60 && totk_frame_cap_applied_now() == 60 && dm_fps_setting() == 60);
    totk_frame_cap_set(30);
    CHECK(totk_frame_cap_get() == 30 && dm_fps_setting() == 30);
    totk_frame_cap_set(45);
    CHECK(totk_frame_cap_get() == 30);
    printf("%u stable presentation checks passed\n", checks);
    return 0;
}
