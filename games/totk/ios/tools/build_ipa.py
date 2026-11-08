#!/usr/bin/env python3
"""Build an unsigned, sideloadable iPhone/iPad IPA from a prepared TOTK work tree."""
import argparse, json, os, plistlib, shutil, subprocess, sys
from pathlib import Path
from compile_seed import METAL_FLAGS
from xcode_dir import developer_dir

APP_FILENAME = 'Tears of the Kingdom.app'

def run(args, cwd, log, env):
    log.parent.mkdir(parents=True,exist_ok=True)
    with log.open('w') as f:
        p=subprocess.run([str(x) for x in args],cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
    if p.returncode:
        tail=log.read_text(errors='replace').splitlines()[-35:]
        raise RuntimeError(f"{args[0]} failed; see {log}\n"+'\n'.join(tail))

def clone(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True)
    if subprocess.run(['cp','-c',str(src),str(dst)],capture_output=True).returncode: shutil.copy2(src,dst)
def copy_tree(src,dst):
    src=Path(src);dst=Path(dst)
    if dst.exists(): shutil.rmtree(dst)
    dst.mkdir(parents=True,exist_ok=True)
    if sys.platform=='darwin':
        result=subprocess.run(['/bin/cp','-cR',str(src)+os.sep+'.',str(dst)],capture_output=True)
        if result.returncode==0: return
        shutil.rmtree(dst);dst.mkdir(parents=True,exist_ok=True)
    for p in src.rglob('*'):
        if p.is_file(): clone(p,dst/p.relative_to(src))

def reusable_seed(seed, source_hash, metal_files):
    """Reuse only the matching seed whose current source and library bytes verify."""
    try:
        cached=json.loads((seed/'seed-manifest.json').read_text())
        files=cached['files']
        entries=list(seed.iterdir())
        if any(p.is_symlink() or not p.is_file() for p in entries): return False
        actual={p.name for p in entries}-{'seed-manifest.json','shader_map.tsv'}
        if actual!=set(files): return False
        if (cached.get('ios_source_manifest_sha256')!=source_hash or
            cached.get('ios_metallibs')!=len(metal_files) or
            cached.get('ios_metal_flags')!=' '.join(METAL_FLAGS)):
            return False
        libraries=[name.removesuffix('.metal')+'.metallib' for name in metal_files]
        if not all(name in files for name in libraries): return False
        import hashlib
        return all(hashlib.sha256((seed/name).read_bytes()).hexdigest()==digest
                   for name,digest in files.items())
    except (OSError,ValueError,KeyError,TypeError,AttributeError):
        return False

def narrow_profile(version, display):
    """Keep the verified narrow preparation separate from the wide-phone image."""
    if display not in ('ipad','duo','iphone'):
        raise RuntimeError('unknown display profile')
    if display=='duo' and version!='v0':
        raise RuntimeError('Duo supports only the verified base 1.0.0')
    return version=='v0' and display in ('ipad','duo')


def experimental_free_autobuild(bundle_id, app_name, cloud_container, version):
    """Keep the trial off the installed production identity and off iCloud saves."""
    if bundle_id == 'com.willspain.totk':
        raise RuntimeError('experimental Free Autobuild must not replace com.willspain.totk')
    if bundle_id != 'com.willspain.totk.freeautobuild':
        raise RuntimeError('experimental Free Autobuild requires --bundle-id com.willspain.totk.freeautobuild')
    if version != 'v0':
        raise RuntimeError('experimental Free Autobuild is base 1.0.0 only')
    if cloud_container:
        raise RuntimeError('experimental Free Autobuild must not use a CloudKit container')
    return 'TOTK Free Autobuild' if app_name == 'Tears of the Kingdom' else app_name


def display_export_env(env, tree, modules, version, display, experimental=False):
    """Export code only, with no inherited relay/audit or mismatched aspect."""
    result=dict(env,TOTK_EXPORT_IOS=str(modules))
    for key in ('TOTK_ASPECT','TOTK_IPAD_ASPECT_PATCH','TOTK_IPHONE_ASPECT_PATCH','TOTK_PCHTXT',
                'TOTK_ASPECT_UI_PATCHED','TOTK_LIVE_DISPLAY','TOTK_LIVE_DISPLAY_AUDIT','DM_DUO_RELAY',
                'TOTK_FREE_AUTOBUILD_EXPORT'):
        result.pop(key,None)
    if narrow_profile(version,display):
        result.update(TOTK_OUTPUT_RES='2752x2064',TOTK_RENDER_RES='2752x2064',TOTK_LIVE_DISPLAY='1',
                      TOTK_ASPECT='4:3',TOTK_IPAD_ASPECT_PATCH='1',TOTK_PCHTXT=str(tree/'build/ipad43.pchtxt'))
    elif version=='v0':
        result.update(TOTK_OUTPUT_RES='1920x1080',TOTK_RENDER_RES='1920x1080',TOTK_ASPECT='239:110',
                      TOTK_IPHONE_ASPECT_PATCH='1',TOTK_PCHTXT=str(tree/'build/iphone239.pchtxt'))
    if experimental:
        result['TOTK_FREE_AUTOBUILD_EXPORT'] = '1'
    return result


def display_project(project, build_number, version, display):
    """Bind bundle opt-in and compiler metadata gate to the same preparation."""
    narrow=narrow_profile(version,display)
    project=project.replace('CFBundleVersion: "18"', f'CFBundleVersion: "{build_number}"')
    project=project.replace('TOTKDisplayAspect: "4:3"','TOTKDisplayAspect: "4:3"\n        TOTKLiveDisplayNarrow: '+('true' if narrow else 'false'))
    project=project.replace('-I../build\n','-I../build -DTOTK_LIVE_DISPLAY_NARROW='+('1' if narrow else '0')+'\n')
    if display=='iphone':
        aspect='239:110' if version=='v0' else '16:9'
        project=project.replace('TOTKOutputResolution: "2752x2064"','TOTKOutputResolution: "1920x1080"').replace('TOTKDisplayAspect: "4:3"',f'TOTKDisplayAspect: "{aspect}"')
    return project


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--tree',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--version',choices=['v0','v112','v121','v143'],required=True);p.add_argument('--logs',type=Path,required=True)
    p.add_argument('--app-name',default='Tears of the Kingdom');p.add_argument('--bundle-id',default='org.macnativegames.totk')
    p.add_argument('--cloud-container', default='', help='optional private CloudKit container from your Apple team')
    p.add_argument('--build-number', type=int, default=27)
    p.add_argument('--app-only',action='store_true',help='prepare a device app bundle for direct signing and installation')
    p.add_argument('--display',choices=['ipad','duo','iphone'],default='ipad',help='ipad/duo: stable 4:3 narrow picture; iphone: static wide display')
    p.add_argument('--experimental-free-autobuild',action='store_true',help='separate base-1.0.0 trial app; does not change com.willspain.totk')
    p.add_argument('--app-icon',type=Path,help='local square JPEG or PNG scaled into the iOS icon set; the source image is not copied into git')
    a=p.parse_args(); tree=a.tree.resolve(); ios=tree/'ios';gen=ios/'Gen';logs=a.logs.resolve(); out=a.out.resolve()
    if a.build_number < 1: raise RuntimeError('build number must be positive')
    narrow=narrow_profile(a.version,a.display)
    if out.suffix.lower()!=('.app' if a.app_only else '.ipa'): raise RuntimeError('iOS output has the wrong extension')
    env=dict(os.environ,DEVELOPER_DIR=developer_dir())
    sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],env=env,text=True).strip()
    subprocess.run(['xcodebuild','-version'],env=env,check=True,stdout=subprocess.DEVNULL)
    if not shutil.which('xcodegen'): raise RuntimeError('install xcodegen: brew install xcodegen')
    gen.mkdir(parents=True,exist_ok=True)
    version_dir=tree/'build'/({'v0':'','v112':'v112','v121':'v121','v143':'v143'}[a.version])
    if a.version=='v0': version_dir=tree/'build'
    for name in ('runtime_tables.h','romfs_config.h'):
        clone(version_dir/name,gen/name)
    clone(tree/'build/shared_fonts_config.h',gen/'shared_fonts_config.h')
    if narrow:
        # Private generation is mandatory; a stale/absent metadata header must
        # never produce an apparently live app with unprepared signed code.
        for name in ('totk-live-display-metadata.json','totk_live_layouts.h'):
            if not (tree/'build'/name).is_file(): raise RuntimeError(f'missing verified live display input: {name}')
        clone(tree/'build/totk_live_layouts.h',gen/'totk_live_layouts.h')
    else:
        (gen/'totk_live_layouts.h').unlink(missing_ok=True)
    # Export the final code after all verified runtime patches have been applied, then embed it in executable segments.
    host=tree/('build/totk-native' if a.version=='v0' else f'build/totk-native-{a.version}')
    rewritten=tree/f'rewritten/{a.version}'; modules=gen/'modules';modules.mkdir(exist_ok=True)
    # Native iPad output must be patched into signed code on the Mac, never into executable pages on the device.
    if a.experimental_free_autobuild:
        if 'FreeAutobuild' not in a.out.name:
            raise RuntimeError('experimental output file name must contain FreeAutobuild')
        a.app_name=experimental_free_autobuild(a.bundle_id,a.app_name,a.cloud_container,a.version)
    e=display_export_env(env,tree,modules,a.version,a.display,a.experimental_free_autobuild)
    run([host,rewritten,'export-ios'],tree,logs/'export-ios.log',e)
    flags=subprocess.check_output([sys.executable,ios/'tools/gen_embed.py',rewritten,gen/'embed.S','--text-dir',modules,'main','subsdk0','sdk'],text=True,env=env).strip()
    # The app's sources use version-specific patch and generated runtime tables.
    project=display_project((ios/'project.yml').read_text(),a.build_number,a.version,a.display)
    # Default is memory-only. CloudKit is bound to the caller's own container.
    entitlements={'com.apple.developer.kernel.increased-memory-limit': True}
    if a.cloud_container:
        if not a.cloud_container.startswith('iCloud.') or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-' for c in a.cloud_container):
            raise RuntimeError('invalid CloudKit container identifier')
        entitlements.update({'com.apple.developer.icloud-container-identifiers':[a.cloud_container],
            'com.apple.developer.icloud-services':['CloudKit'],
            'com.apple.developer.icloud-container-environment':'Development'})
    (gen/'TOTK.entitlements').write_bytes(plistlib.dumps(entitlements))
    project=project.replace('Sources/TOTK.entitlements','Gen/TOTK.entitlements')
    if a.cloud_container: project=project.replace('TOTKDisplayAspect: "4:3"','TOTKDisplayAspect: "4:3"\n        TOTKCloudContainer: '+a.cloud_container)
    settings=gen/'Settings.bundle';copy_tree(ios/'Settings.bundle',settings)
    if a.display=='iphone':
        root=settings/'Root.plist';info=plistlib.loads(root.read_bytes())
        for row in info['PreferenceSpecifiers']:
            if row.get('Key')=='TOTKResolution':
                if a.version=='v0':row.update(Titles=['1920 × 1080','1600 × 900','1280 × 720','960 × 540'],Values=['1080','900','720','540'],DefaultValue='1080')
                else:row.update(Titles=['1920 × 1080','1600 × 900','1280 × 720'],Values=['1080','900','720'],DefaultValue='1080')
            if row.get('Type')=='PSGroupSpecifier' and row.get('Title')=='Display':
                row['FooterText']='Restart Tears of the Kingdom after changing resolution. '+('All settings fill the iPhone screen; 960 × 540 uses one quarter as many render pixels as 1920 × 1080.' if a.version=='v0' else 'Choose a lower resolution for smoother performance.')
        root.write_bytes(plistlib.dumps(info))
    if a.version=='v143': project=project.replace('          - loader.c','          - loader.c\n          - game_patches.c')
    else: project=project.replace('          - loader.c','          - loader.c\n          - game_patches_v143.c')
    if a.version!='v0': project=project.replace('-I../build\n','-I../build -DTOTK_GAME_'+a.version.upper()+'=1\n')
    if a.app_icon:
        icon=a.app_icon.expanduser().resolve()
        if not icon.is_file(): raise RuntimeError('app icon must be a local image file')
        run([sys.executable,ios/'tools/prepare_app_icon.py','--image',icon,'--output',gen/'Assets.xcassets'],tree,logs/'app-icon.log',env)
        project=project.replace('      - path: Gen/Settings.bundle\n        type: folder\n        buildPhase: resources\n',
            '      - path: Gen/Settings.bundle\n        type: folder\n        buildPhase: resources\n'
            '      - path: Gen/Assets.xcassets\n        buildPhase: resources\n')
        project=project.replace('CODE_SIGNING_ALLOWED: NO\n','CODE_SIGNING_ALLOWED: NO\n      ASSETCATALOG_COMPILER_APPICON_NAME: AppIcon\n')
    (ios/'project.generated.yml').write_text(project)
    # Build device dependencies separately: Homebrew's Mac archives cannot be linked into an iOS application.
    deps=tree/'build/ios-deps'
    run([sys.executable,ios/'tools/build_deps.py',deps],tree,logs/'ios-dependencies.log',env)
    shutil.copytree(deps/'include',gen/'deps/include',dirs_exist_ok=True)
    (gen/'deps/lib').mkdir(parents=True,exist_ok=True)
    for lib in ('libopus.a','libvpx.a'): clone(deps/'lib'/lib,gen/'deps/lib'/lib)
    seed_src=tree/f'build/shader-seed/{a.version}'; seed=gen/'totk/shader-seed'
    if not (seed_src/'seed-manifest.json').is_file(): raise RuntimeError(f'missing verified shader seed: {seed_src}')
    import hashlib
    source_manifest_hash=hashlib.sha256((seed_src/'seed-manifest.json').read_bytes()).hexdigest()
    metal_files=[name for name in json.loads((seed_src/'seed-manifest.json').read_text())['files'] if name.endswith('_b.metal')]
    reuse_seed=reusable_seed(seed,source_manifest_hash,metal_files)
    if not reuse_seed:
        if seed.exists(): shutil.rmtree(seed)
        copy_tree(seed_src,seed)
        run([sys.executable,ios/'tools/compile_seed.py',seed,'iphoneos'],tree,logs/'ios-shaders.log',env)
    seed_manifest=json.loads((seed/'seed-manifest.json').read_text())
    seed_manifest['ios_source_manifest_sha256']=source_manifest_hash
    (seed/'seed-manifest.json').write_text(json.dumps(seed_manifest,indent=2)+'\n')
    (seed/'shader_map.tsv').write_text('# TOTK control-code-sha256-v1\n')
    # Regenerate file hashes after adding iOS metallibs and the tiny runtime map.
    manifest=json.loads((seed/'seed-manifest.json').read_text())
    # shader_map.tsv is read in place (DM_NVN_SHMAP); the importer accepts only shader files, so it stays out of the list.
    manifest['files']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(seed.iterdir())
                       if f.is_file() and f.name not in ('seed-manifest.json','shader_map.tsv')}
    (seed/'seed-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Generate the app project with resolved identity and the embedded-segment linker flags.
    env.update(TOTK_APP_NAME=a.app_name,TOTK_BUNDLE_ID=a.bundle_id)
    xcconfig=gen/'totk.xcconfig'
    frameworks=' '.join('-framework '+x for x in ['UIKit','Metal','MetalFX','Foundation','CloudKit','QuartzCore','ImageIO','CoreGraphics','CoreText','UniformTypeIdentifiers','CoreFoundation','AudioToolbox','GameController','CoreMotion','CoreHaptics','AVFAudio','Security'])
    xcconfig.write_text(f'OTHER_LDFLAGS = $(inherited) {frameworks} Gen/deps/lib/libopus.a Gen/deps/lib/libvpx.a {flags}\n')
    run(['xcodegen','generate','--spec','project.generated.yml'],ios,logs/'xcodegen.log',env)
    derived=tree/'build/ios-derived';run(['xcodebuild','-project','TOTKIOS.xcodeproj','-scheme','TOTKIOS','-configuration','Release',
        '-destination','generic/platform=iOS','-derivedDataPath',derived,'CODE_SIGNING_ALLOWED=NO','CODE_SIGNING_REQUIRED=NO','CODE_SIGN_IDENTITY='],
        ios,logs/'xcodebuild-ios.log',env)
    app=derived/'Build/Products/Release-iphoneos/TOTK.app'
    if not app.is_dir(): raise RuntimeError(f'Xcode did not produce {app}')
    # Carry the increased-memory-limit entitlement in the executable's ad-hoc signature so re-signing tools keep it;
    # the game's 3.2 GiB pool exceeds the default iOS footprint limit.
    run(['codesign','--force','--sign','-','--entitlements',gen/'TOTK.entitlements',app/'TOTK'],ios,logs/'ios-entitlements.log',env)
    res=app/'GameData';res.mkdir(exist_ok=True)
    romfs=tree/f'extracted/{a.version}/romfs'
    copy_tree(romfs,res/'romfs')
    patches=tree/f'extracted/{a.version}/romfs-patches'
    if a.version=='v0': patches=tree/f'extracted/{a.version}'/('romfs-patches-43' if narrow else 'romfs-patches-iphone239')
    for src,dst in [(patches,res/'romfs-patches'),
                    (tree/f'extracted/{a.version}/romfs-patches-out',res/'romfs-patches-out'),
                    (tree/'extracted/firmware/shared-fonts',res/'shared-fonts')]: copy_tree(src,dst)
    copy_tree(seed,res/'shader-seed')
    # The first frame waits only for the title screen's shaders (pso_title.tsv); the runtime looks for the list next to
    # DM_NVN_SHMAP, which is in shader-seed on iOS. Without it the first frame waits for the whole warm-up.
    # Exported from the one shared shader list (games/totk/assets/shader_list.tsv, docs/shader-list.md).
    run([sys.executable,tree/'scripts/shader_list.py','export',tree/'assets/shader_list.tsv',logs/'pso_manifest.tsv',
         res/'shader-seed/pso_title.tsv'],tree,logs/'shader-list.log',env)
    # Keep the player's first install self-contained; the app stores saves and writable shader cache in Documents.
    if a.app_only:
        copy_tree(app,out)
        print(f'Device app ready: {out}; sign and install directly with devicectl')
        return
    payload=tree/'build/ios-payload';shutil.rmtree(payload,ignore_errors=True);(payload/'Payload').mkdir(parents=True)
    copy_tree(app,payload/'Payload'/APP_FILENAME)
    out.parent.mkdir(parents=True,exist_ok=True);out.unlink(missing_ok=True)
    run(['zip','-qry','-0',out,'Payload'],payload,logs/'ipa-zip.log',env)
    print(f'IPA ready: {out} ({out.stat().st_size/2**30:.1f} GiB); unsigned, sideload with your signing tool')

if __name__=='__main__':
    try: main()
    except (RuntimeError,subprocess.CalledProcessError,OSError) as e: print(f'ERROR: {e}',file=sys.stderr);sys.exit(1)
