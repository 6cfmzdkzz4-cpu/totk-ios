#!/usr/bin/env python3
"""Prepare the verified base-1.0.0 display overlay. SPDX-License-Identifier: GPL-3.0-only.
Uses the separately licensed TOTK-Ultrawide build-time library; generated files stay local.
"""
import argparse,hashlib,json,shutil,struct,sys,tempfile
from pathlib import Path
import compression.zstd as zstd

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('tree',type=Path);parser.add_argument('--profile',choices=['ipad','iphone'],default='ipad');args=parser.parse_args()
tree=args.tree.resolve();iphone=args.profile=='iphone';label='iphone239' if iphone else 'ipad43'
vendor=root/'vendor/totk-ultrawide'
sys.path.insert(0,str(vendor))
from romfs_script import patch_romfs

def sha(data): return hashlib.sha256(data).hexdigest()
def sarc(data):
    assert data[:4]==b'SARC' and data[6:8]==b'\xff\xfe'
    header,size,start=struct.unpack_from('<HxxII',data,4);assert size==len(data)
    count=struct.unpack_from('<H',data,header+6)[0];names=header+12+count*16+8
    out={}
    for i in range(count):
        _,attr,lo,hi=struct.unpack_from('<4I',data,header+12+i*16)
        off=names+(attr&0xffffff)*4;name=data[off:data.index(0,off,start)].decode()
        out[name]=(start+lo,start+hi)
    return out

image=tree/'rewritten/v0/main.patched.bin'
expected=json.loads((tree/'assets/expected-v1.0.0.json').read_text())
assert sha(image.read_bytes())==expected['rewritten_sha256']['main.patched.bin']
template_path=vendor/('exefs_template/super169/main-1.0.0.pchtxt' if iphone else 'exefs_template/sub169/main-1.0.0.pchtxt')
if True:
    pinned={x['path']:x['sha'] for x in json.loads((vendor/'tree.json').read_text())['tree'] if x['type']=='blob'}
    for source in [template_path,vendor/'romfs_script.py',vendor/'conversion.py']:
        data=source.read_bytes();assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==pinned[source.relative_to(vendor).as_posix()]
template=template_path.read_text()
build_id=(tree/'extracted/v0/exefs/main').read_bytes()[0x40:0x54].hex().upper()
assert '@nsobid-'+build_id in template
ratio=239/110 if iphone else 4/3
scaling=ratio/(16/9) if ratio>=16/9 else (16/9)/ratio
data_float=struct.pack('<f',scaling).hex().upper()
rows={}
for line in template.splitlines():
    parts=line.split()
    if len(parts)!=2 or len(parts[0])!=8: continue
    try: at=int(parts[0],16)
    except ValueError: continue
    if at in (0x377ac54,0x1968c2c,0x1e5eae8): continue # exact camera/data veneers are handled by the native runtime
    value=parts[1].replace('{hex4}',data_float)
    assert len(value)==8 and bytes.fromhex(value)
    rows[at]=value
out=tree/f'build/{label}.pchtxt'
out.write_text('@nsobid-'+build_id+'\n@enabled\n'+'\n'.join(f'{at:08X} {value}' for at,value in rows.items())+'\n@stop\n')

index=json.loads((tree/'extracted/v0/romfs/index.json').read_text());assert index['status']=='pass'
files={x['path']:x for x in index['files']}
with (tree/'extracted/v0/romfs/romfs.bin').open('rb') as rom:
    item=files['Pack/ZsDic.pack.zs'];rom.seek(item['offset']);pack=zstd.decompress(rom.read(item['size']))
dictionaries={}
for lo,hi in sarc(pack).values():
    d=zstd.ZstdDict(pack[lo:hi]);dictionaries[d.dict_id]=d
base=tree/'extracted/v0/romfs-patches';dest=tree/('extracted/v0/romfs-patches-iphone239' if iphone else 'extracted/v0/romfs-patches-43')
shutil.rmtree(dest,ignore_errors=True);shutil.copytree(base,dest)
archive='UI/LayoutArchive/Common.Product.100.Nin_NX_NVN.blarc.zs'
compressed=(dest/archive).read_bytes();info=zstd.get_frame_info(compressed);dictionary=dictionaries[info.dictionary_id]
before=zstd.decompress(compressed,zstd_dict=dictionary);blarc=bytearray(before);members=sarc(before)
changed=[]
with tempfile.TemporaryDirectory(prefix=f'totk-{label}-',dir=tree/'build') as tmp:
    tmp=Path(tmp)
    for name,(lo,hi) in members.items():
        p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(before[lo:hi])
    patch_romfs(str(tmp),ratio,'corner')
    if iphone:
        pane=(tmp/'blyt/PlayerStatus_00.bflyt').read_bytes();at=pane.index(b'RootPane')+0x38
        assert abs(struct.unpack_from('<f',pane,at)[0]-(16/9)/ratio)<1e-6
    for name,(lo,hi) in members.items():
        data=(tmp/name).read_bytes();assert len(data)==hi-lo
        if data!=before[lo:hi]:
            assert name.startswith(('blyt/','anim/'))
            changed.append(name);blarc[lo:hi]=data
assert len(changed)>100
compressed=zstd.compress(bytes(blarc),level=19,zstd_dict=dictionary)
assert zstd.decompress(compressed,zstd_dict=dictionary)==bytes(blarc)
(dest/archive).write_bytes(compressed)
lines=[]
for line in (dest/'manifest.tsv').read_text().splitlines():
    if line.startswith(archive+'\t'):
        parts=line.split('\t');parts[1:3]=[str(len(compressed)),sha(compressed)];line='\t'.join(parts)
    lines.append(line)
(dest/'manifest.tsv').write_text('\n'.join(lines)+'\n')
result={'status':'pass','aspect':'239:110' if iphone else '4:3','community_source':json.loads((vendor/'source.json').read_text()),
        'viewport_scale':scaling,'hud_scale':(16/9)/ratio if iphone else ratio/(16/9),
        'code_patch_rows':len(rows),'code_patch_sha256':sha(out.read_bytes()),'ui_members_changed':len(changed),
        'ui_overlay_sha256':sha(compressed),'original_quick_menu_and_game_over_fixes_retained':True}
(tree/f'analysis/{label}-preparation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
