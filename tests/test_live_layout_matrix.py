"""Run the private exact-guest matrix fixture explicitly; CI has no game inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import tempfile
import compression.zstd as zstd


def run(tree: Path, paired: bool = False) -> dict:
    """Verify exact inputs and dispose all compiler/fixture products after use."""
    tree = tree.resolve()
    expected = json.loads((tree/"assets/expected-v1.0.0.json").read_text())
    image = tree/"rewritten/v0/main.patched.bin"
    elf = tree/"extracted/v0/elf/main.elf"
    sdk = tree/"rewritten/v0/sdk.patched.bin"
    inventory = json.loads((elf.parent/"main-inventory.json").read_text())
    for path, digest in [(image, expected["rewritten_sha256"][image.name]), (elf, inventory["elf_sha256"]),
                         (sdk, expected["rewritten_sha256"][sdk.name])]:
        with path.open("rb") as source:
            assert hashlib.file_digest(source, "sha256").hexdigest() == digest, path
    assert inventory["build_id"].startswith("082ce09b06e33a123cb1e2770f5f9147709033db")
    assert all(segment["elf_file_offset"] == segment["address"]+4096 for segment in inventory["segments"])
    tags = dict(inventory["dynamic_tags"])
    constants = {0x45404f8: "_ZN2nn4util6detail7FloatPiE", 0x4540530: "_ZN2nn4util6detail14FloatDegree180E",
                 0x4540658: "_ZN2nn4util6detail19AngleIndexHalfRoundE", 0x4540660: "_ZN2nn4util6detail17SinCosSampleTableE"}
    exports = {row["name"]: row for row in json.loads((elf.parent/"sdk-inventory.json").read_text())["exports"]}
    carrier = elf.read_bytes()[4096:]
    bindings = {}
    for at in range(tags[7], tags[7]+tags[8], 24):
        where, kind, addend = struct.unpack_from("<QQq", carrier, at)
        if where not in constants:continue
        assert kind & 0xffffffff == 1025 and addend == 0
        symbol = tags[6]+(kind >> 32)*24
        name_offset = tags[5]+struct.unpack_from("<I", carrier, symbol)[0]
        name = carrier[name_offset:carrier.index(0, name_offset)].decode()
        assert name == constants[where] and exports[name]["type"] == 1 and exports[name]["defined"]
        bindings[where] = exports[name]["value"]
    assert set(bindings) == set(constants)
    with tempfile.TemporaryDirectory(prefix="totk-matrix-fixture-") as folder:
        executable = Path(folder)/"matrix-fixture"
        binding_file = Path(folder)/"sdk-math-bindings.bin"
        binding_file.write_bytes(b"".join(struct.pack("<QQ", where, value) for where, value in sorted(bindings.items())))
        runtime = Path(__file__).resolve().parents[1]/"games/totk/runtime"
        includes=[];resource_file=Path(folder)/"player-status.bflyt";parts_file=Path(folder)/"app-map.bflyt"
        world_file=Path(folder)/"player-3d.bflyt";extra_file=Path(folder)/"extra-stamina.bflyt";map_modes=Path(folder)/"map-mode-fields.bin"
        guide_file=Path(folder)/"control-guide.bflyt";save_file=Path(folder)/"save-load-icon.bflyt"
        map_mode_evidence=None;overlay_evidence=None
        for payload in [resource_file,parts_file,world_file,extra_file,map_modes,guide_file,save_file]:payload.write_bytes(b"")
        if paired:
            from importlib.util import module_from_spec, spec_from_file_location
            spec=spec_from_file_location("emit_live_display",Path(__file__).resolve().parents[1]/"tools/emit_live_display.py")
            emitter=module_from_spec(spec);spec.loader.exec_module(emitter)
            emitter.emit(tree/"build/totk-live-display-metadata.json",Path(folder)/"totk_live_layouts.h")
            includes=["-I"+folder,"-DFIXTURE_PAIRED=1","-DTOTK_TEST_SIGNED_DISPLAY=1"]
            import sys
            sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
            from live_display_resources import sarc_members,parse_animation,parse_layout
            metadata=json.loads((tree/"build/totk-live-display-metadata.json").read_text())
            archive="UI/LayoutArchive/Common.Product.100.Nin_NX_NVN.blarc.zs"
            compressed=(tree/"extracted/v0/romfs-patches-43"/archive).read_bytes()
            assert hashlib.sha256(compressed).hexdigest()==metadata["prepared_overlay_sha256"]
            romfs=tree/"extracted/v0/romfs"
            row=next(r for r in json.loads((romfs/"index.json").read_text())["files"] if r["path"]=="Pack/ZsDic.pack.zs")
            with (romfs/"romfs.bin").open("rb") as stream:
                stream.seek(row["offset"]);dictionary_pack=stream.read(row["size"])
            dictionaries={d.dict_id:d for data in sarc_members(zstd.decompress(dictionary_pack)).values() for d in [zstd.ZstdDict(data)]}
            members=sarc_members(zstd.decompress(compressed,zstd_dict=dictionaries[zstd.get_frame_info(compressed).dictionary_id]))
            resource_file.write_bytes(members["blyt/PlayerStatus_00.bflyt"])
            parts_file.write_bytes(members["blyt/AppMap_00.bflyt"])
            world_file.write_bytes(members["blyt/PlayerStatus3D_00.bflyt"])
            extra_file.write_bytes(members["blyt/PaExtraStaminaGauge_00.bflyt"])
            overlay_evidence={}
            for name,path,role in [("CtrlGuide_00",guide_file,"bottom center"),("SaveLoadIcon_00",save_file,"bottom left")]:
                data=members["blyt/"+name+".bflyt"];path.write_bytes(data)
                animations={key:{"sha256":hashlib.sha256(value).hexdigest(),"geometry_float_values":sum(t["tag"]=="FLPA" for t in parse_animation(value).values())}
                            for key,value in members.items() if key.startswith("anim/"+name+"_") and key.endswith(".bflan")}
                overlay_evidence[name]=dict(resource_sha256=hashlib.sha256(data).hexdigest(),safe_role=role,
                                           prepared_panes=parse_layout(data),animations=animations,
                                           tested="both active within same completed interval; Open asymmetric insets and Closed right84; exact resource mutation rejects complete source")
            animation=members["anim/AppMap_00_MiniMap.bflan"];tracks={}
            for offset,track in parse_animation(animation).items():
                if track["tag"]=="FLPA" and track["component"]=="value" and track["target"] in (0,1,8,9):
                    tracks.setdefault((track["name"],track["target"]),[]).append(struct.unpack_from("<2f",animation,offset-4))
            map_layout=next(p for p in metadata["layouts"] if p["name"]=="AppMap_00")
            names=["N_Cut_00","N_Cut_01","N_Cut_02","N_Cut_03","P_MapCapture_00"];states=[]
            for frame in (0,1):
                state=[]
                for name in names:
                    pane=next(p for p in map_layout["panes"] if p["name"]==name);fields=[]
                    for target,baseline in zip((0,1,8,9),pane["position"]+pane["size"]):
                        eligible=[(at,value) for at,value in tracks.get((name,target),[]) if at<=frame]
                        fields.append(eligible[-1][1] if eligible else baseline)
                    state.append(fields)
                states.append(state)
            map_modes.write_bytes(struct.pack("<40f",*(value for state in states for fields in state for value in fields)))
            map_mode_evidence=dict(animation="AppMap_00_MiniMap",sha256=hashlib.sha256(animation).hexdigest(),
                                   field_order=["x","y","width","height"],pane_order=names,full_map_frame0=states[0],minimap_frame1=states[1])
            spec=next(p for p in metadata["layouts"] if p["name"]=="PlayerStatus_00")
            assert spec["resource_qualified"] and hashlib.sha256(resource_file.read_bytes()).hexdigest()==spec["resource_sha256"]
        subprocess.run(["xcrun", "clang", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        *includes, "-I"+str(runtime), "-I"+str(runtime.parent/"third_party/d2r_gfx/gfx/nvn/include"),
                        str(Path(__file__).with_suffix(".c")), str(runtime/"totk_live_display.c"),
                        str(runtime/"hle_stack.c"), str(runtime/"hle_stack.S"),
                        str(Path(__file__).with_name("test_live_hle_entry.S")), "-lm", "-pthread", "-o", str(executable)], check=True)
        result = subprocess.run([str(executable), str(image), str(elf), str(tags[7]), str(tags[8]), str(Path(folder)/"owners.txt"),
                                 str(sdk), str(binding_file),str(resource_file),str(parts_file),str(world_file),str(extra_file),str(map_modes),str(guide_file),str(save_file)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"CPU matrix fixture failed ({result.returncode}): {result.stderr[-6000:]}")
        receipt = json.loads(result.stdout)
        sources = [Path(__file__).with_suffix(".c"), Path(__file__), runtime/"totk_live_display.c",
                   runtime/"totk_live_display.h", runtime/"totk_live_reservations.h", runtime/"hle_stack.c", runtime/"hle_stack.S",
                   Path(__file__).with_name("test_live_hle_entry.S"), runtime/"aspect_patch.h",
                   Path(__file__).resolve().parents[1]/"tools/emit_live_display.py",
                   Path(__file__).resolve().parents[1]/"tools/prepare_live_display.py",
                   Path(__file__).resolve().parents[1]/"tools/live_display_resources.py"]
        public = Path(__file__).resolve().parents[1]
        receipt.update(source_sha256={str(path.relative_to(public)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
                       private_inputs="main rewrite, ELF carrier and SDK rewrite hashes verified; four SDK math globals resolved by exact symbol identity",
                       raw_local_coverage=dict(cases=240, euler_order="Rz*Ry*Rx", parent_origin_codes=[0, 1, 2],
                                               varying_parent_dimensions=True, magnification_flags=True,
                                               nonidentity_parent_world=True, user_matrix_restore=True),
                       host_stack="production hle_stack.c/S, TSD restore, nested callbacks, tracing0/1/2, GPR/FP sentinels and 4KiB guest slice; fixture stubs thread-note/handle/clock services",
                       paired_private_fixture=paired,
                       safe_area_adaptation="CPU: asymmetric top-left insets, immutable inset/orientation generations; actual gameplay still pending" if paired else False,
                       admission_negative_controls=["required root missing","wrong pane kind","receiving eui Scissor substituted for authored Alignment","wrong explicit scene-space Parts resource","same copied name with wrong resource SHA","geometry appeared during an empty-container Calculate","visible full-map authored endpoint","minimap endpoint changed during original Calculate","capture default without isolated40 basis","capture context basis mismatch","unknown absolute/global matrix outside capture","unknown absolute/global matrix inside primary capture tree"] if paired else [],
                       leaf_draw_coverage="synthetic full-size SDK-kind panes; non-Pane leaf virtual dispatch redirected to real common Calculate, no fonts/materials/GPU services; real Alignment called independently",
                       safe_bottom_right_gameplay="pending actual minimap/environment/command proof",
                       positive_parts_namespace="real PaExtraStaminaGauge resource bound to aliased root, two descendant transforms, no nested root double scaling" if paired else None,
                       minimap_mode_evidence=map_mode_evidence,
                       ordinary_overlay_evidence=overlay_evidence,
                       capture_islands={"derived_methods":"exact receiving eui methods223206c and115eca8 executed intact in default/perspective branches; fixture supplies both projection and memcpy services, not real projection allocators/helpers",
                                        "cases":8,"domains":"nonidentity capture view and distinct preserved default GlobalMtx; main-scene correction suspended through text, circle, scissor and opaque Parts with an SDK-unknown nested widget",
                                        "preservation":"DrawInfo projection/view restored; capture GlobalMtx kept; circles round; alpha calculation and clip dimensions stable across both postures",
                                        "raster_alpha_and_clips":"pending actual game/GPU; CPU fixture checks Calculate matrices, alpha bytes and authored clip dimensions only"} if paired else None,
                       metadata_sha256=hashlib.sha256((tree/"build/totk-live-display-metadata.json").read_bytes()).hexdigest() if paired else None,
                       signed_ios=False, signed_export_cpu=paired,
                       owner_lifecycle="eui entry eight-register/return adapter, copied names, address reuse, failure and deleting-destructor invalidation; real SDK Build body/allocator not exercised",
                       compiler_products="disposed automatically", device_operations=False)
        return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-tree", type=Path, required=True)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--paired", action="store_true")
    args = parser.parse_args()
    result = run(args.private_tree,args.paired)
    if args.receipt:args.receipt.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps(result, indent=2))
