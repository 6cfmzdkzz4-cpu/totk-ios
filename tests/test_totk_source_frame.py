"""Source-only receiving TOTK presentation and JPEG binding regressions."""
from pathlib import Path
import hashlib
import json
import os
import platform
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
NVN=ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn'
RUNTIME=ROOT/'games/totk/runtime'

def body(source,signature):
    start=source.index(signature);opening=source.index('{',start);depth,end=1,opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

@unittest.skipUnless(platform.system()=='Darwin','Receiving Objective-C fixtures require Foundation')
class TotkSourceFrameTests(unittest.TestCase):
    def test_actual_replay_composition_and_mfx(self):
        mtl=(NVN/'nvn_mtl.m').read_text();hle=(NVN/'hle_nvn.c').read_text();mfx=(NVN/'totk_mfx.m').read_text()
        fixture=(ROOT/'tests/source_frame/fixture.m.in').read_text()
        for marker,source,signature in [
            ('VIEWPORT',mtl,'static MTLViewport present_viewport('),
            ('MFX_RESET',mfx,'void mfx_geometry_reset(void)'),
            ('MFX_PREPARE',mfx,'int mfx_frame_prepare('),
            ('MFX_SOURCE',mfx,'TotkLiveSource mfx_output_source('),
            ('PRESENT_EXEC',hle,'static void present_exec(win_t *o, int idx, double period, const int crop[4], uint64_t frame, TotkLiveSource source) {')]:
            fixture=fixture.replace('/* '+marker+' */',body(source,signature))
        start=hle.index('typedef struct { int kind, n;');end=hle.index(';',hle.index('} xjob;',start))+1
        fixture=fixture.replace('/* XJOB */',hle[start:end])
        with tempfile.TemporaryDirectory(prefix='totk-source-frame-') as temporary:
            p=Path(temporary);src=p/'fixture.m';binary=p/'fixture';src.write_text(fixture)
            c=subprocess.run(['xcrun','clang','-fobjc-arc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined,float-divide-by-zero','-I',str(NVN),'-framework','Foundation',str(src),'-o',str(binary)],capture_output=True,text=True,timeout=60)
            self.assertEqual(c.returncode,0,c.stdout+c.stderr)
            r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertIn('1000 immutable TOTK source transitions',r.stdout)

    def test_metadata_producer_and_capture_order(self):
        hle=(NVN/'hle_nvn.c').read_text();mtl=(NVN/'nvn_mtl.m').read_text()
        producer=body(hle,'NVN(nvnQueuePresentTexture, void, NVNqueue *q, NVNwindow *w, int idx) {')
        self.assertEqual(producer.count('runtime_live_display_finish_interval(o->presents)'),1)
        self.assertIn('.source=source',producer);self.assertIn('j.frame, j.source)',hle)
        present=body(mtl,'uint64_t mtl_present_source(')
        self.assertLess(present.index('!totk_source_valid'),present.index('mfx_present_frame('))
        self.assertLess(present.index('!totk_source_valid'),present.index('duo_capture_source('))
        self.assertLess(present.index('duo_capture_source('),present.index('pthread_mutex_lock(&g_pq_mu)'))
        viewport=body(mtl,'static MTLViewport present_viewport(')
        self.assertNotIn('runtime_aspect_now',viewport)
        capture=body(mtl,'static void duo_capture_source(')
        self.assertIn('encodeWaitForEvent:g_ev value:g_fence',capture)
        self.assertIn('encodeSignalEvent:complete value:sequence',capture)
        self.assertIn('[g_q4 waitForEvent:complete value:sequence]',capture)
        self.assertIn('totk_duo_capture_pixels(pixels.contents,width,height,row,1,source_interval)',capture)
        for forbidden in ('nextDrawable','front_metal_layer','g_pq_done','pthread_mutex_lock(&g_pq_mu)'):
            self.assertNotIn(forbidden,capture)

    def test_actual_jpeg_manifest_binds_immutable_source(self):
        driver=r'''
#import <Foundation/Foundation.h>
#include "totk_duo_capture.h"
int main(void){@autoreleasepool{
 unsigned char pixels[478*220*4];memset(pixels,127,sizeof pixels);
 TotkLiveSource source={.frame=77,.generation=9,.requested_generation=11,.aspect=239.0/110,.width=239,.height=110,.scale=2,.safe_insets={0,10,5,0},.valid=1,.fallback=1,.hud_adjustments=17};
 totk_duo_capture_pixels(pixels,478,220,478*4,1,source);
 source.valid=0;source.frame=78;totk_duo_capture_pixels(pixels,478,220,478*4,1,source);
 source.valid=1;source.aspect=4.0/3;totk_duo_capture_pixels(pixels,478,220,478*4,1,source);
 return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix='totk-jpeg-bind-') as temporary:
            p=Path(temporary);src=p/'driver.m';binary=p/'writer';src.write_text(driver)
            c=subprocess.run(['xcrun','clang','-fobjc-arc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I',str(RUNTIME),str(src),str(RUNTIME/'totk_duo_capture.m'),
                '-framework','Foundation','-framework','ImageIO','-framework','UniformTypeIdentifiers','-framework','CoreGraphics','-o',str(binary)],capture_output=True,text=True,timeout=60)
            self.assertEqual(c.returncode,0,c.stdout+c.stderr)
            result=subprocess.run([str(binary)],env=dict(os.environ,DM_DUO_RELAY=str(p)),capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            manifest=json.loads((p/'backend.json').read_text());jpeg=(p/'frame.jpg').read_bytes()
            self.assertEqual(manifest['game'],'Tears of the Kingdom');self.assertEqual(manifest['source_frame'],77)
            self.assertEqual(manifest['source_geometry_generation'],9);self.assertEqual(manifest['source_safe_insets'],[0,10,5,0])
            self.assertEqual(manifest['frame_sha256'],hashlib.sha256(jpeg).hexdigest());self.assertEqual(manifest['relay_images'],1)
            self.assertEqual([manifest['width'],manifest['height']],[478,220]);self.assertEqual(manifest['source_aspect'],239/110)
            self.assertFalse(manifest['camera']['enabled']);self.assertFalse(manifest['hud']['enabled'])
            self.assertEqual(manifest['diagnostics']['bound_interval_hud_adjustments'],17)
            self.assertIsNone(manifest['diagnostics']['camera_callback_count'])
            dimensions=subprocess.check_output(['sips','-g','pixelWidth','-g','pixelHeight',str(p/'frame.jpg')],text=True)
            self.assertIn('pixelWidth: 478',dimensions);self.assertIn('pixelHeight: 220',dimensions)

if __name__=='__main__':unittest.main()
