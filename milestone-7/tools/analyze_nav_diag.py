"""Read navigation JSONL without rewriting it or printing private field values."""
import argparse
import collections
import json
import pathlib
import sys


def summarize(paths):
    sessions = {}
    navigation_sessions = {}
    timing = {}
    invalid = 0
    duplicate = 0
    seen_records = set()
    for path in paths:
        with pathlib.Path(path).open('rb') as stream:
            for number, raw in enumerate(stream, 1):
                try:
                    row = json.loads(raw.decode('utf-8'))
                    key = (row['pid'], row['run_mono_ns'], row['session'])
                    identity = key + (row['record'], row['sequence'])
                    if row.get('version') != 1:
                        raise ValueError('unsupported format version')
                except (ValueError, KeyError, TypeError, UnicodeError):
                    invalid += 1
                    continue
                if identity in seen_records:
                    duplicate += 1
                    continue
                seen_records.add(identity)
                navigation_id = row.get('navigation_session_id')
                if navigation_id:
                    route = navigation_sessions.setdefault(navigation_id, {
                        'navigation_session_id': navigation_id, 'source_label': row.get('source', 'unspecified'),
                        'source_kind': row.get('source_kind', 'legacy_manual_label'), 'application_id': row.get('application_id'),
                        'wall_time': None, 'wall_time_valid': False, 'start_mono_ns': None,
                        'messages': collections.Counter(), 'roundabout_updates': []})
                    if row['record'] == 'navigation_session_start':
                        route.update(wall_time=row.get('wall_time'), wall_time_valid=row.get('wall_time_valid', False),
                                     wall_time_quality=row.get('wall_time_quality'), start_mono_ns=row.get('mono_ns'))
                    if row['record'] == 'navigation':
                        route['messages']['0x%04x' % row['message_id']] += 1
                        values = {f['path']: f['value'] for f in row.get('fields', [])
                                  if isinstance(f.get('value'), (int, float)) and not isinstance(f.get('value'), bool)}
                        maneuver = values.get('steps[0].maneuver.type')
                        if maneuver in (30, 31, 32, 33, 34, 35, 43, 44, 45, 46):
                            # Numeric wire fields only. Never infer actual intersection geometry or app identity.
                            if len(route['roundabout_updates']) < 2000:
                                route['roundabout_updates'].append({'mono_ns': row.get('mono_ns'),
                                    'sequence': row['sequence'], 'type': maneuver,
                                    'exit_number': values.get('steps[0].maneuver.exit_number'),
                                    'exit_angle': values.get('steps[0].maneuver.exit_angle')})
                if row['record'] == 'navigation_timing':
                    stage = row.get('stage', 'unknown')
                    stats = timing.setdefault(stage, {'records': 0, 'rx_correlated': 0, 'max_since_rx_ms': None})
                    stats['records'] += 1
                    rx = row.get('rx_mono_ns', 0)
                    now = row.get('mono_ns', 0)
                    if rx and now >= rx:
                        stats['rx_correlated'] += 1
                        delay = (now-rx)/1000000.0
                        stats['max_since_rx_ms'] = max(stats['max_since_rx_ms'] or 0, delay)
                if key not in sessions:
                    sessions[key] = {'pid': key[0], 'run_mono_ns': key[1], 'session': key[2],
                                     'source_label': row.get('source', 'unspecified'),
                                     'retained_messages': 0, 'messages': collections.Counter(),
                                     'max_seen': 0, 'max_captured': 0, 'max_lost': 0, 'max_truncated': 0,
                                     'malformed_captured_bodies': 0, 'field_limited_messages': 0,
                                     'unknown_fields': collections.Counter(), 'known_paths': collections.Counter()}
                result = sessions[key]
                for name in ('seen', 'captured', 'lost', 'truncated'):
                    result['max_' + name] = max(result['max_' + name], row.get(name, 0))
                if row['record'] != 'navigation':
                    continue
                result['retained_messages'] += 1
                result['messages']['0x%04x' % row['message_id']] += 1
                result['malformed_captured_bodies'] += bool(row.get('malformed'))
                result['field_limited_messages'] += bool(row.get('field_limit'))
                for field in row.get('fields', []):
                    if 'field' not in field:
                        continue
                    if field.get('known'):
                        result['known_paths'][field['path']] += 1
                    else:
                        name = '%s / wire=%s / size=%s' % (field['path'], field.get('wire'), field.get('size'))
                        result['unknown_fields'][name] += 1
    return {'format': 1, 'invalid_lines': invalid, 'duplicate_records': duplicate,
            'application_identity': 'source is a manual label; application_id is not inferred',
            'navigation_sessions': [navigation_sessions[k] for k in sorted(navigation_sessions)],
            'timing': timing,
            'counter_scope': 'Recorder/run cumulative; not per-session totals; loss is diagnostic queue loss, not AA packet loss',
            'sessions': [sessions[k] for k in sorted(sessions)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='+', type=pathlib.Path)
    args = parser.parse_args()
    try:
        result = summarize(args.files)
    except OSError as error:
        parser.exit(1, str(error) + '\n')
    json.dump(result, sys.stdout, ensure_ascii=True, indent=2)
    sys.stdout.write('\n')
    return 2 if result['invalid_lines'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
