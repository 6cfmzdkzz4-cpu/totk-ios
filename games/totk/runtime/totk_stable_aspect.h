/* Stable narrow-picture fit for the verified base-1.0.0 4:3 preparation.
   The live scene path used to publish the device aspect into the camera and
   stretch the 4:3 framebuffer across the drawable. Two visible failures follow:
   a 1920x1080 cutscene is drawn through that retargeted viewport, so the movie
   is stretched; an unqualified dialogue layout increments the HUD miss count,
   the next interval forces 4:3, and presentation then adds side bars only while
   the text box is up. This fit keeps one 4:3 picture for gameplay and dialogue.
   A 16:9 movie is letterboxed inside that same picture. */
#pragma once
#include "dm_display_geometry.h"
#include <stdlib.h>
#include <string.h>

#define TOTK_STABLE_CONTENT_ASPECT (4.0 / 3.0)
#define TOTK_CUTSCENE_ASPECT (16.0 / 9.0)

static inline int totk_stable_aspect_on(void) {
    const char *value = getenv("TOTK_STABLE_ASPECT");
    return value && !strcmp(value, "1");
}

static inline int totk_stable_insets_ok(double width, double height, double left, double top, double right, double bottom) {
    return isfinite(width) && isfinite(height) && isfinite(left) && isfinite(top) && isfinite(right) && isfinite(bottom) &&
           width > 0 && height > 0 && left >= 0 && top >= 0 && right >= 0 && bottom >= 0 &&
           left + right < width && top + bottom < height;
}

/* Center the verified 4:3 picture in the scene's safe rectangle. Wider scenes
   keep side bars; taller scenes keep bars above and below. The bars belong to
   the scene, so a dialogue layout cannot move them. */
static inline dm_display_geometry totk_stable_scene_picture(double width, double height, double scale,
                                                            double left, double top, double right, double bottom) {
    if (!totk_stable_insets_ok(width, height, left, top, right, bottom)) return (dm_display_geometry){0};
    dm_display_geometry picture = dm_display_make(width - left - right, height - top - bottom, scale, TOTK_STABLE_CONTENT_ASPECT);
    if (!picture.valid) return picture;
    picture.picture.x += left;
    picture.picture.y += top;
    return picture;
}

/* Fit a 16:9 movie inside an already fitted picture. The result stays inside
   the picture, so the scene's side bars do not move. */
static inline dm_display_rect totk_cutscene_in_picture(dm_display_rect picture) {
    dm_display_geometry fitted = dm_display_make(picture.width, picture.height, 1, TOTK_CUTSCENE_ASPECT);
    if (!fitted.valid) return (dm_display_rect){0};
    return (dm_display_rect){picture.x + fitted.picture.x, picture.y + fitted.picture.y,
                             fitted.picture.width, fitted.picture.height};
}
