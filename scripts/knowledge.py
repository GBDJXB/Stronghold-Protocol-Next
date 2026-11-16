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
    if 'operators' in manifest:
        operators = manifest['operators']
        require(type(operators) is list and
                all(type(n) is str and bool(n) and n.strip() == n for n in operators),
                'operators must be canonical names')
        require(len(operators) == len(set(operators)), 'duplicate operator name')
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
        labels = rows[0].find_all(string=re.compile('激活所需人数'))
        require(len(labels) == 1, f'{name}: missing unique activation count')
        count = re.fullmatch(r'激活所需人数\s+([1-9][0-9]*)',
                             labels[0].parent.get_text(' ', strip=True))
        require(count is not None, f'{name}: invalid activation count')
        cells = rows[1].find_all('td', recursive=False)
        require(len(cells) == 1, f'{name}: missing effect cell')
        effect = text_with_breaks(cells[0])
        require(bool(effect), f'{name}: empty effect')
        records[name] = {'name': name, 'activation_count': int(count[1]),
                         'effect_source_text': effect,
                         'locator': {'file': 'source.html', 'table_index_1based': table_positions[id(table)],
                                     'heading': name, 'effect_row_1based': 2, 'effect_cell_1based': 1}}
    if 'operators' in manifest:
        operators = extract_operators(soup, manifest['operators'], table_positions)
        for name, record in records.items():
            table = matches[name][0].find_parent('table')
            rows = table.find('tbody', recursive=False).find_all('tr', recursive=False)
            require(all(len(row.find_all('td', recursive=False)) == 1 for row in rows),
                    f'{name}: expected one cell per covenant row')
            member_row = rows[2]
            names = [a['title'] for a in member_row.find_all('a', title=True)
                     if a['title'] in operators]
            require(len(names) == len(set(names)), f'{name}: duplicate sampled member')
            record['members'] = []
            record['members_complete'] = False
            for operator_name, operator in operators.items():
                forward = operator_name in names
                reverse = name in [m['name'] for m in operator['memberships']]
                require(forward == reverse, f'{name}/{operator_name}: membership conflict')
                if forward:
                    record['members'].append({'name': operator_name,
                        'locator': {'file': 'source.html',
                                    'table_index_1based': table_positions[id(table)],
                                    'row_1based': 3, 'anchor_title': operator_name}})
        return records, operators
    return records


def extract_operators(soup, selected, table_positions):
    matches = {name: [] for name in selected}
    for table in soup.select('table.wikitable.mw-collapsible.mw-collapsed'):
        body = table.find('tbody', recursive=False)
        if body is None:
            continue
        rows = body.find_all('tr', recursive=False)
        headers = [[cell.get_text(strip=True) for cell in row.find_all(['td', 'th'], recursive=False)]
                   for row in rows]
        if not any(len(header) == 3 and header[1:] == ['盟约', '特质'] for header in headers):
            continue
        for index, row in enumerate(rows, 1):
            cells = row.find_all('td', recursive=False)
            if not cells:
                continue
            names = {a['title'] for a in cells[0].find_all('a', title=True)
                     if a.find_parent('tr') is row} & matches.keys()
            require(len(names) <= 1, 'ambiguous selected operator identity in roster row')
            for name in names:
                matches[name].append((table, index, cells))
    records = {}
    for name in sorted(selected):
        require(len(matches[name]) == 1, f'{name}: expected one operator roster row')
        table, row_index, cells = matches[name][0]
        require(len(cells) == 3, f'{name}: expected three operator cells')
        locator = {'file': 'source.html', 'table_index_1based': table_positions[id(table)],
                   'row_1based': row_index, 'anchor_title': name}
        memberships = text_with_breaks(cells[1]).splitlines()
        require(bool(memberships) and len(memberships) == len(set(memberships)),
                f'{name}: missing or duplicate membership')
        trait = copy.deepcopy(cells[2])
        labels = [div for div in trait.find_all('div', recursive=False)
                  if re.search(r'position\s*:\s*absolute', div.get('style', ''))]
        require(len(labels) == 1 and bool(labels[0].get_text(strip=True)),
                f'{name}: missing unique trait type')
        trait_type = labels[0].get_text(strip=True)
        labels[0].decompose()
        arrows = trait.select('.mdi-arrow-right')
        require(len(arrows) <= 1, f'{name}: unsupported trait segment structure')
        # Split the DOM at the observed arrow, without inferring gameplay tiers.
        parts = ['']
        for node in trait.descendants:
            if isinstance(node, bs4.element.Tag) and 'mdi-arrow-right' in node.get('class', []):
                parts.append('')
            elif isinstance(node, bs4.element.Tag) and node.name == 'br':
                parts[-1] += '\n'
            elif isinstance(node, bs4.element.NavigableString) and not isinstance(node, bs4.element.Comment):
                parts[-1] += str(node)
        segments = ['\n'.join(line.strip() for line in part.splitlines() if line.strip())
                    for part in parts]
        require(all(segments), f'{name}: empty trait segment')
        records[name] = {'name': name, 'locator': locator,
            'memberships': [{'name': member, 'locator': dict(locator, cell_1based=2)}
                            for member in memberships],
            'trait': {'type_source_text': trait_type,
                      'segments': [{'index_1based': i, 'source_text': text}
                                   for i, text in enumerate(segments, 1)],
                      'locator': dict(locator, cell_1based=3)}}
    return records


def make_snapshot(snapshot_id, manifest, raw):
    validate_manifest(manifest, raw)
    records = extract(raw, manifest)
    operators = None
    if 'operators' in manifest:
        records, operators = records
    snapshot = {'format_version': FORMAT_VERSION, 'snapshot_id': snapshot_id,
                'manifest': manifest, 'source': manifest['source'],
                'applicability': manifest['applicability'],
                'extractor': {'version': EXTRACTOR_VERSION, 'python': sys.version.split()[0],
                              'beautifulsoup4': bs4.__version__, 'soupsieve': version('soupsieve'),
                              'typing_extensions': version('typing_extensions'), 'parser': 'html.parser'},
                'coverage': {'covenants': sorted(records), 'operators': [],
                             'members_complete': False, 'season_complete': False},
                'covenants': records}
    if operators is not None:
        snapshot['operators'] = operators
        snapshot['coverage']['operators'] = sorted(operators)
        snapshot['extractor']['capability'] = 'operators-1'
    return snapshot


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
    recorded = snapshot['extractor']
    require(type(recorded) is dict and set(recorded) == set(expected['extractor']),
            'invalid extractor metadata schema')
    require(type(recorded['version']) is str and recorded['version'] == EXTRACTOR_VERSION,
            'unsupported extractor version')
    require(recorded['parser'] == 'html.parser', 'unsupported extractor parser')
    if 'capability' in expected['extractor']:
        require(recorded['capability'] == 'operators-1', 'unsupported extractor capability')
    for dependency in ('python', 'beautifulsoup4', 'soupsieve', 'typing_extensions'):
        value = recorded[dependency]
        require(type(value) is str and re.fullmatch(
            r'[0-9]+(?:\.[0-9]+)*(?:(?:a|b|rc)[0-9]+)?(?:\.post[0-9]+)?'
            r'(?:\.dev[0-9]+)?(?:\+[a-z0-9]+(?:[.-][a-z0-9]+)*)?', value) is not None,
            f'invalid extractor {dependency} version')
    # Runtime dependencies may change while supported extraction remains identical.
    # Keep original build provenance; compare all other fields with a fresh parse.
    expected['extractor'] = recorded
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
    directory = args.store / args.snapshot_id
    require(directory.exists(), f'snapshot_id not found: {args.snapshot_id}')
    require(directory.is_dir() and all((directory / name).is_file() for name in
            ('snapshot.json', 'snapshot.sha256', 'source.html')),
            f'incomplete snapshot archive: {args.snapshot_id}')
    snapshot = load_snapshot(directory, args.snapshot_id)
    result = {key: snapshot[key] for key in ('snapshot_id', 'source', 'applicability', 'coverage', 'extractor')}
    if args.command == 'candidates':
        candidates = {}
        records = snapshot.get('covenants' if args.kind == 'covenant' else 'operators', {})
        for name, record in records.items():
            candidates[name] = {'name': name, 'locators': [record['locator']]}
            if args.kind == 'covenant':
                candidates[name]['effect_status'] = 'found'
        if args.kind == 'covenant':
            for operator in snapshot.get('operators', {}).values():
                for membership in operator['memberships']:
                    candidate = candidates.setdefault(membership['name'], {
                        'name': membership['name'], 'effect_status': 'not_in_sample', 'locators': []})
                    candidate['locators'].append(membership['locator'])
        result.update({'status': 'found', 'kind': args.kind,
                       'data': [candidates[name] for name in sorted(candidates)]})
        return result
    record = copy.deepcopy(snapshot['covenants' if args.kind == 'covenant' else 'operators'].get(args.name)) if args.kind == 'covenant' or 'operators' in snapshot else None
    if record and args.kind == 'operator':
        for membership in record['memberships']:
            covenant = snapshot['covenants'].get(membership['name'])
            membership['effect_status'] = 'found' if covenant else 'not_in_sample'
            if covenant:
                membership['covenant'] = covenant
    result.update({'status': 'found', 'kind': args.kind, 'data': record} if record else {
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
    q.add_argument('--kind', choices=['covenant', 'operator'], required=True)
    q.add_argument('--name', required=True)
    c = sub.add_parser('candidates')
    c.add_argument('--kind', choices=['covenant', 'operator'], required=True)
    for command in (b, q, c):
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
