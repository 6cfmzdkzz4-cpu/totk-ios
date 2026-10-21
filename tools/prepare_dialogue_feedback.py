#!/usr/bin/env python3
"""Build an exact NPC dialogue-flow allowlist from structured actions in the local game."""
import argparse
import hashlib
import json
from pathlib import Path

from feedback_common import (EVENTSETTING_COMPRESSED_SHA256, EVENTSETTING_DECOMPRESSED_SHA256,
                             EVENTSETTING_PATH, VerifiedRomFS, eventsetting_rows, load_evfl)


def generate(romfs: Path, header: Path, report_path: Path, evfl_source: Path | None = None,
             emit: bool = True):
    evfl, parser_identity = load_evfl(evfl_source)
    source = VerifiedRomFS(romfs)
    index = source.index
    read = source.read
    selected = []
    skipped = []
    rows = [r for r in index['files'] if r['path'].startswith('Event/EventFlow/Npc_') and r['path'].endswith('.bfevfl.zs')]
    for row in rows:
        data = read(row['path'])
        flow = evfl.EventFlow()
        flow.read(data)
        chart = flow.flowchart
        if not chart:
            skipped.append(row['path'])
            continue
        dialogue = set()
        for event in chart.events:
            action = event.data
            if isinstance(action, evfl.ActionEvent) and action.actor_action.v.v == 'EventTalk' and action.actor.v.identifier.name.startswith('Npc_'):
                dialogue.add(event)
        talk_entries = []
        for entry in chart.entry_points:
            queue = [entry.main_event.v]
            seen = set()
            while queue:
                event = queue.pop()
                if event is None or event in seen:
                    continue
                seen.add(event)
                item = event.data
                if hasattr(item, 'nxt'):
                    queue.append(item.nxt.v)
                if isinstance(item, evfl.SwitchEvent):
                    queue.extend(x.v for x in item.cases.values())
                if isinstance(item, evfl.ForkEvent):
                    queue.extend(x.v for x in item.forks)
                    queue.append(item.join.v)
                if isinstance(item, evfl.SubFlowEvent) and not item.res_flowchart_name:
                    queue.extend(x.main_event.v for x in chart.entry_points if x.name == item.entry_point_name)
            if seen & dialogue:
                talk_entries.append(entry.name)
        if talk_entries:
            assert chart.name == flow.name and flow.name.startswith('Npc_') and len(flow.name.encode()) < 128
            selected.append({'name': flow.name, 'path': row['path'], 'sha256': hashlib.sha256(data).hexdigest(),
                             'talk_entries_with_reachable_npc_EventTalk_action': sorted(talk_entries)})
        else:
            skipped.append(row['path'])
    selected.sort(key=lambda r: r['name'])
    assert len({r['name'] for r in selected}) == len(selected) and len(selected) > 300
    eventsetting_data = source.read_checked(EVENTSETTING_PATH, EVENTSETTING_COMPRESSED_SHA256,
                                            EVENTSETTING_DECOMPRESSED_SHA256)
    eventsetting = eventsetting_rows(eventsetting_data)
    by_name = {r['name']: r for r in selected}
    runtime_rows = []
    allowed_components = {'NPCTalk', 'NPCGatherTalk', 'NPCChaseTalk'}
    for row in eventsetting:
        flow_name = Path(row['FlowchartFileName']).stem
        component = Path(row['EventSettingComponentList']).name.split('.')[0]
        source = by_name.get(flow_name)
        if not source or not component.startswith(tuple(allowed_components)) or row['EntryPointName'] not in source['talk_entries_with_reachable_npc_EventTalk_action']:
            continue
        name = row['__RowId']
        assert name.isascii() and 0 < len(name) < 128
        runtime_rows.append({**row, 'flowchart_sha256': source['sha256']})
    runtime_rows.sort(key=lambda r: r['__RowId'])
    assert len({r['__RowId'] for r in runtime_rows}) == len(runtime_rows) and len(runtime_rows) > 300
    lines = ['/* Exact runtime NPCTalk event IDs joined to reachable structured NPC EventTalk actions. */',
             'static const char *const npc_dialogue_flows[] = {']
    lines.extend('    '+json.dumps(r['__RowId'])+',' for r in runtime_rows)
    lines.extend(['};', '#define NPC_DIALOGUE_FLOW_COUNT (sizeof npc_dialogue_flows / sizeof npc_dialogue_flows[0])', ''])
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text('\n'.join(lines))
    report = {'source_romfs_sha256': index['romfs_sha256'], 'npc_flowcharts_parsed': len(rows),
              'dialogue_flows_selected': len(selected), 'excluded_without_proven_local_Talk_to_EventTalk_path': skipped,
              'runtime_npc_event_ids': len(runtime_rows), 'runtime_rows': runtime_rows,
              'eventsetting_path': EVENTSETTING_PATH,
              'eventsetting_compressed_sha256': EVENTSETTING_COMPRESSED_SHA256,
              'eventsetting_decompressed_sha256': EVENTSETTING_DECOMPRESSED_SHA256,
              'parser_repository': parser_identity['repository'],
              'parser_commit': parser_identity['commit'],
              'parser_source_tree_sha256': parser_identity['source_tree_sha256'],
              'header_sha256': hashlib.sha256(header.read_bytes()).hexdigest(), 'flows': selected,
              'scope': 'A pulse on entry into an exact active NPC conversation event ID. The compiled category and reachable EventTalk path are proven; the live script branch and every text-page change are not observed.'}
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    if emit:
        print(json.dumps({k: report[k] for k in ['npc_flowcharts_parsed', 'dialogue_flows_selected', 'runtime_npc_event_ids']}))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('romfs', type=Path)
    parser.add_argument('header', type=Path)
    parser.add_argument('report', type=Path)
    parser.add_argument('--evfl-source', type=Path,
                        help='optional checkout or package directory at the pinned EVFL commit')
    args = parser.parse_args()
    generate(args.romfs, args.header, args.report, args.evfl_source)


if __name__ == '__main__':
    main()
