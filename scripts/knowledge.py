"""Independent, offline static knowledge CLI. No capture or OCR dependencies."""
import argparse
import copy
from datetime import datetime
import hashlib
from importlib.metadata import version
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile

import bs4
from bs4 import BeautifulSoup

FORMAT_VERSION = 1
EXTRACTOR_VERSION = '1.0.0'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f'duplicate JSON key: {key}')
            result[key] = value
        return result
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_pairs)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def validate_manifest(manifest, raw):
    require(type(manifest) is dict and type(manifest.get('format_version')) is int and
            manifest['format_version'] == FORMAT_VERSION,
            'unsupported manifest format_version')
    names = manifest.get('covenants')
    require(type(names) is list and bool(names) and
            all(type(n) is str and n.strip() == n and bool(n) for n in names),
            'covenants must be nonempty canonical names')
    require(len(names) == len(set(names)), 'duplicate covenant name')
    source = manifest['source']
    require(type(source) is dict, 'source must be an object')
    for field in ('url', 'acquired_at', 'sha256', 'attribution'):
        require(type(source[field]) is str and bool(source[field].strip()), f'missing source {field}')
    require(source['url'].startswith(('https://', 'http://')), 'source URL must identify original page')
    acquired = datetime.fromisoformat(source['acquired_at'].replace('Z', '+00:00'))
    require(acquired.tzinfo is not None, 'acquired_at needs timezone')
    require(re.fullmatch('[a-f0-9]{64}', source['sha256']) is not None and
            source['sha256'] == digest(raw), 'source SHA256 mismatch')
    require(source['page_revision'] is None or
            type(source['page_revision']) is int and source['page_revision'] > 0,
            'page_revision must be positive integer or null')
    require(source['dependency_revisions'] is None or type(source['dependency_revisions']) is dict,
            'dependency_revisions must be object or null')
    limitations = source['limitations']
    require(type(limitations) is list and all(type(x) is str and bool(x.strip()) for x in limitations),
            'limitations must be text list')
    require(source['dependency_revisions'] is not None or bool(limitations),
            'unknown dependencies require explicit limitation')
    applicability = manifest['applicability']
    require(type(applicability) is dict and set(applicability) == {'season', 'sequence', 'client_build'},
            'applicability requires separate season, sequence, client_build')
    require(all(v is None or type(v) is str and bool(v.strip()) for v in applicability.values()),
            'applicability values must be text or null')
    revisions = re.findall(r'"wgRevisionId"\s*:\s*(\d+)', raw.decode('utf-8', errors='strict'))
    require(len(revisions) <= 1, 'duplicate page revision')
    if revisions:
        require(source['page_revision'] == int(revisions[0]), 'page revision contradicts source')
    else:
        require(source['page_revision'] is None, 'page revision unavailable in source')


def text_with_breaks(element):
    element = copy.deepcopy(element)
    for br in element.find_all('br'):
        br.replace_with('\n')
    return '\n'.join(line.strip() for line in element.get_text().splitlines() if line.strip())


def extract(raw, manifest):
    soup = BeautifulSoup(raw.decode('utf-8', errors='strict'), 'html.parser')
    selected = set(manifest['covenants'])
    matches = {name: [] for name in selected}
    for bold in soup.find_all('b'):
        name = bold.get_text(strip=True)
        if name in matches:
            matches[name].append(bold)
    tables = soup.find_all('table')
    table_positions = {id(table): i for i, table in enumerate(tables, 1)}
    records = {}
    for name in sorted(selected):
        require(len(matches[name]) == 1, f'{name}: expected one covenant heading')
        table = matches[name][0].find_parent('table')
        require(table is not None, f'{name}: missing table')
        body = table.find('tbody', recursive=False)
        require(body is not None, f'{name}: missing tbody')
        rows = body.find_all('tr', recursive=False)
        require(len(rows) == 3 and '盟约所属干员' in rows[2].get_text(),
                f'{name}: unexpected covenant table structure')
        require(matches[name][0].find_parent('tr') is rows[0], f'{name}: misplaced heading')
        counts = re.findall(r'激活所需人数\s*(\d+)', rows[0].get_text(' ', strip=True))
        require(len(counts) == 1 and int(counts[0]) > 0, f'{name}: missing activation count')
        cells = rows[1].find_all('td', recursive=False)
        require(len(cells) == 1, f'{name}: missing effect cell')
        effect = text_with_breaks(cells[0])
        require(bool(effect), f'{name}: empty effect')
        records[name] = {'name': name, 'activation_count': int(counts[0]),
                         'effect_source_text': effect,
                         'locator': {'file': 'source.html', 'table_index_1based': table_positions[id(table)],
                                     'heading': name, 'effect_row_1based': 2, 'effect_cell_1based': 1}}
    return records


def make_snapshot(snapshot_id, manifest, raw):
    validate_manifest(manifest, raw)
    records = extract(raw, manifest)
    return {'format_version': FORMAT_VERSION, 'snapshot_id': snapshot_id,
                'manifest': manifest, 'source': manifest['source'],
                'applicability': manifest['applicability'],
                'extractor': {'version': EXTRACTOR_VERSION, 'python': sys.version.split()[0],
                              'beautifulsoup4': bs4.__version__, 'soupsieve': version('soupsieve'),
                              'typing_extensions': version('typing_extensions'), 'parser': 'html.parser'},
                'coverage': {'covenants': sorted(records), 'operators': [],
                             'members_complete': False, 'season_complete': False},
                'covenants': records}


def load_snapshot(directory, snapshot_id):
    raw_json = (directory / 'snapshot.json').read_bytes()
    require(digest(raw_json) == (directory / 'snapshot.sha256').read_text(encoding='ascii').strip(),
            'snapshot SHA256 mismatch')
    snapshot = read_json(directory / 'snapshot.json')
    require(type(snapshot) is dict and type(snapshot.get('format_version')) is int and
            snapshot['format_version'] == FORMAT_VERSION,
            'unsupported snapshot format_version')
    require(snapshot['snapshot_id'] == snapshot_id, 'snapshot_id mismatch')
    expected = make_snapshot(snapshot_id, snapshot['manifest'], (directory / 'source.html').read_bytes())
    # A checksum detects damage, while reconstruction checks semantics and provenance.
    # Neither is a signature against an attacker who can replace the entire archive.
    require(json.dumps(snapshot, sort_keys=True) == json.dumps(expected, sort_keys=True),
            'snapshot structure, provenance, extractor or extracted records mismatch')
    return snapshot


def build(args):
    manifest = read_json(args.manifest)
    raw = args.source.read_bytes()
    snapshot = make_snapshot(args.snapshot_id, manifest, raw)
    args.store.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.pending-', dir=args.store))
    try:
        (stage / 'source.html').write_bytes(raw)
        (stage / 'snapshot.json').write_text(json.dumps(snapshot, ensure_ascii=False, sort_keys=True,
                                                       indent=2) + '\n', encoding='utf-8')
        (stage / 'snapshot.sha256').write_text(digest((stage / 'snapshot.json').read_bytes()) + '\n',
                                              encoding='ascii')
        load_snapshot(stage, args.snapshot_id)
        require(not (args.store / args.snapshot_id).exists(), 'snapshot_id already exists')
        # Windows rename fails if destination exists, including concurrent publishers.
        stage.rename(args.store / args.snapshot_id)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return {'status': 'built', 'snapshot_id': args.snapshot_id}


def query(args):
    snapshot = load_snapshot(args.store / args.snapshot_id, args.snapshot_id)
    result = {key: snapshot[key] for key in ('snapshot_id', 'source', 'applicability', 'coverage', 'extractor')}
    record = snapshot['covenants'].get(args.name)
    result.update({'status': 'found', 'kind': 'covenant', 'data': record} if record else {
        'status': 'not_in_sample', 'name': args.name, 'meaning': 'Not collected; does not imply absence from game'})
    return result


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    b = sub.add_parser('build')
    b.add_argument('--source', type=Path, required=True)
    b.add_argument('--manifest', type=Path, required=True)
    q = sub.add_parser('query')
    q.add_argument('--kind', choices=['covenant'], required=True)
    q.add_argument('--name', required=True)
    for command in (b, q):
        command.add_argument('--store', type=Path, required=True)
        command.add_argument('--snapshot-id', required=True)
    args = parser.parse_args()
    try:
        require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,99}', args.snapshot_id) is not None,
                'invalid snapshot_id')
        print(json.dumps(build(args) if args.command == 'build' else query(args), ensure_ascii=False))
        return 0
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        print(json.dumps({'status': 'error', 'message': str(error)}, ensure_ascii=False))
        return 1


if __name__ == '__main__':
    sys.exit(main())
