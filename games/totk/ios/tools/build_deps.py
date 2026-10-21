#!/usr/bin/env python3
"""Build the audio and VP9 static libraries for a physical iOS device."""
import hashlib, json, os, subprocess, sys
from source_checkout import checkout
from pathlib import Path

out = Path(sys.argv[1]).resolve(); out.mkdir(parents=True, exist_ok=True)
sdk = subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'], text=True).strip()
src = out/'src'; build = out/'build'; src.mkdir(exist_ok=True); build.mkdir(exist_ok=True)
sdklink=out/'sdkroot/Platforms/iPhoneOS.platform/Developer/SDKs'/Path(sdk).name
if sdklink.is_symlink(): sdklink.unlink()
sdklink.parent.mkdir(parents=True,exist_ok=True)
if not sdklink.exists(): sdklink.symlink_to(sdk)
def run(cmd, cwd=None, env=None): subprocess.run(cmd, cwd=cwd, env=env, check=True)

OPUS_COMMIT='22244de5a79bd1d6d623c32e72bf1954b56235be'  # v1.6.1
VPX_COMMIT='1024874c5919305883187e2953de8fcb4c3d7fa6'   # v1.16.0
opus=src/'opus'; checkout('https://github.com/xiph/opus.git',OPUS_COMMIT,opus)
opus_build=build/'opus-ninja'
run(['cmake','-G','Ninja','-S',str(opus),'-B',str(opus_build),'-DCMAKE_SYSTEM_NAME=iOS',f'-DCMAKE_OSX_SYSROOT={sdklink}',
     '-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0','-DOPUS_BUILD_SHARED_LIBRARY=OFF',
     '-DOPUS_BUILD_TESTING=OFF','-DOPUS_BUILD_PROGRAMS=OFF','-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF','-DCMAKE_BUILD_TYPE=Release'])
run(['cmake','--build',str(opus_build),'--config','Release','-j'])

vpx=src/'libvpx'; checkout('https://github.com/webmproject/libvpx.git',VPX_COMMIT,vpx)
vpxout=build/'libvpx'; vpxout.mkdir(exist_ok=True)
bindir=build/'ios-bin'; bindir.mkdir(exist_ok=True)
real_sdk=sdk; real_tools=Path(os.environ.get('DEVELOPER_DIR',subprocess.check_output(['xcode-select','-p'],text=True).strip()))/'Toolchains/XcodeDefault.xctoolchain/usr/bin'
for tool in ('clang','clang++','ar','as','strip'):
    link=bindir/tool
    if link.is_symlink(): link.unlink()
    if not link.exists(): link.symlink_to(real_tools/tool)
wrapper=bindir/'xcrun'
wrapper.write_text(f'''#!/usr/bin/env python3
import subprocess,sys
r=subprocess.run(["/usr/bin/xcrun",*sys.argv[1:]],capture_output=True,text=True)
s=r.stdout.replace({real_sdk!r},{str(sdklink)!r}).replace({str(real_tools)!r},{str(bindir)!r})
sys.stdout.write(s);sys.stderr.write(r.stderr);sys.exit(r.returncode)
''')
wrapper.chmod(0o755)
vpx_env=dict(os.environ,PATH=str(bindir)+os.pathsep+os.environ.get('PATH',''))
run([str(vpx/'configure'),'--target=arm64-darwin-gcc','--disable-examples','--disable-tools',
     '--disable-docs','--disable-unit-tests','--disable-webm-io','--enable-pic','--disable-shared','--enable-static',
     f'--extra-cflags=-target arm64-apple-ios26.0 -isysroot {sdklink} -miphoneos-version-min=26.0'], cwd=vpxout, env=vpx_env)
run(['make','-j',str(os.cpu_count() or 4),'libvpx.a'],cwd=vpxout,env=vpx_env)
lib=out/'lib'; include=out/'include'; lib.mkdir(exist_ok=True); include.mkdir(exist_ok=True)
import shutil
shutil.copy2(opus_build/'libopus.a',lib/'libopus.a')
shutil.copy2(vpxout/'libvpx.a',lib/'libvpx.a')
shutil.copytree(opus/'include',include,dirs_exist_ok=True)
shutil.copytree(vpx/'vpx',include/'vpx',dirs_exist_ok=True)
shutil.copy2(vpxout/'vpx_config.h',include/'vpx_config.h')
receipt={'opus_commit':OPUS_COMMIT,'libvpx_commit':VPX_COMMIT,
         'libraries':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(lib.glob('*.a'))},
         'sdk':Path(sdk).name}
(out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(f'IOS_DEPS={out}')
