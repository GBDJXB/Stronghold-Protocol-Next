"""Public offline CLI tests; all committed source material is synthetic."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CLI = Path(__file__).with_name('knowledge.py')
HTML = '''<html><script>"wgRevisionId":7</script><table><tbody>
<tr><td><b>Example</b><div>激活所需人数 <b>2</b></div></td></tr>
<tr><td>First <span>+10%</span><br>Second &lt;condition&gt;.</td></tr>
<tr><td><table><tbody><tr><th>盟约所属干员</th></tr>
<tr><td><a title="Operator A">A</a></td></tr></tbody></table></td></tr>
</tbody></table></html>'''


class KnowledgeCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source.html'
        self.source.write_text(HTML, encoding='utf-8')
        self.manifest = self.root / 'manifest.json'
        self.config = {
            'format_version': 1, 'covenants': ['Example'],
            'source': {'url': 'https://example.invalid/source',
                       'acquired_at': '2026-11-15T12:00:00Z',
                       'sha256': hashlib.sha256(self.source.read_bytes()).hexdigest(),
                       'page_revision': 7, 'attribution': 'Synthetic test author',
                       'dependency_revisions': None,
                       'limitations': ['Historical template revisions unknown']},
            'applicability': {'season': None, 'sequence': None, 'client_build': None}}
        self.save_manifest()

    def save_manifest(self):
        self.manifest.write_text(json.dumps(self.config), encoding='utf-8')

    def cli(self, *args, success=True):
        result = subprocess.run([sys.executable, '-X', 'utf8', str(CLI), *map(str, args)],
                                capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0 if success else 1, result.stderr)
        return json.loads(result.stdout)

    def build(self, snapshot='one', success=True):
        return self.cli('build', '--source', self.source, '--manifest', self.manifest,
                        '--store', self.root / 'store', '--snapshot-id', snapshot, success=success)

    def query(self, name='Example', snapshot='one', success=True):
        return self.cli('query', '--store', self.root / 'store', '--snapshot-id', snapshot,
                        '--kind', 'covenant', '--name', name, success=success)

    def test_fixed_source_build_and_explicit_query(self):
        self.build()
        result = self.query()
        self.assertEqual(result['data']['name'], 'Example')
        self.assertEqual(result['data']['activation_count'], 2)
        self.assertEqual(result['data']['effect_source_text'], 'First +10%\nSecond <condition>.')
        self.assertEqual(result['snapshot_id'], 'one')
        self.assertEqual(result['source']['page_revision'], 7)
        self.assertEqual(result['applicability']['client_build'], None)
        self.assertIn('locator', result['data'])
        self.assertFalse(result['coverage']['members_complete'])
        self.assertEqual(self.query('Other')['status'], 'not_in_sample')

    def test_activation_count_requires_the_complete_canonical_positive_integer(self):
        for index, count in enumerate(['2.5', '2e3', '+2', '-2', '0', '02', '2 people', '2x', '2/3', '２']):
            with self.subTest(count=count):
                self.source.write_text(HTML.replace('<b>2</b>', f'<b>{count}</b>'), encoding='utf-8')
                self.config['source']['sha256'] = hashlib.sha256(self.source.read_bytes()).hexdigest()
                self.save_manifest()
                snapshot = f'invalid-{index}'
                failed = self.build(snapshot, success=False)
                self.assertIn('activation count', failed['message'])
                self.query(snapshot=snapshot, success=False)

    def test_historical_build_environment_is_preserved_when_reparse_matches(self):
        self.build()
        directory = self.root / 'store' / 'one'
        path = directory / 'snapshot.json'
        data = json.loads(path.read_bytes())
        historical = dict(data['extractor'], python='3.13.6', beautifulsoup4='4.14.2',
                          soupsieve='2.8.2', typing_extensions='4.14.0')
        data['extractor'] = historical
        path.write_text(json.dumps(data), encoding='utf-8')
        (directory / 'snapshot.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest())
        result = self.query()
        self.assertEqual(result['extractor'], historical)
        self.assertEqual(result['data']['effect_source_text'], 'First +10%\nSecond <condition>.')
        self.assertEqual(json.loads(path.read_bytes()), data)

    def test_invalid_or_unsupported_extractor_metadata_is_rejected(self):
        self.build()
        directory = self.root / 'store' / 'one'
        path = directory / 'snapshot.json'
        original = path.read_bytes()
        for field, value, error in [
                ('version', '9.0.0', 'unsupported extractor version'),
                ('parser', 'other.parser', 'unsupported extractor parser'),
                ('python', 3.13, 'invalid extractor python version'),
                ('beautifulsoup4', '', 'invalid extractor beautifulsoup4 version'),
                ('soupsieve', None, 'invalid extractor soupsieve version'),
                ('typing_extensions', 'unknown', 'invalid extractor typing_extensions version'),
                ('extra', 'unexpected', 'invalid extractor metadata schema')]:
            with self.subTest(field=field):
                data = json.loads(original)
                data['extractor'][field] = value
                path.write_text(json.dumps(data), encoding='utf-8')
                (directory / 'snapshot.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertIn(error, self.query(success=False)['message'])

    def test_invalid_input_never_publishes_and_preserves_existing_snapshot(self):
        self.build()
        self.source.write_text(HTML.replace('First', 'Changed'), encoding='utf-8')
        self.build('bad-hash', success=False)
        self.query(snapshot='bad-hash', success=False)
        self.assertEqual(self.query()['data']['effect_source_text'], 'First +10%\nSecond <condition>.')
        self.source.write_text(HTML, encoding='utf-8')
        for field, value in [('format_version', 99), ('covenants', ['Example', 'Example']),
                             ('covenants', []), ('applicability', {})]:
            with self.subTest(field=field, value=value):
                original = self.config[field]
                self.config[field] = value
                self.save_manifest()
                self.build('invalid', success=False)
                self.query(snapshot='invalid', success=False)
                self.config[field] = original
        self.save_manifest()
        for html in [HTML.replace('<tbody>', '', 1), HTML + HTML,
                     HTML.replace('First <span>+10%</span><br>Second &lt;condition&gt;.', '')]:
            self.source.write_text(html, encoding='utf-8')
            self.config['source']['sha256'] = hashlib.sha256(self.source.read_bytes()).hexdigest()
            self.save_manifest()
            self.build('invalid', success=False)
        self.source.write_bytes(b'\xff')
        self.config['source']['sha256'] = hashlib.sha256(self.source.read_bytes()).hexdigest()
        self.save_manifest()
        self.build('invalid', success=False)

    def test_corrupt_or_unsupported_snapshot_is_rejected_even_with_recomputed_hash(self):
        self.build()
        directory = self.root / 'store' / 'one'
        path = directory / 'snapshot.json'
        original = path.read_bytes()
        for mutation in ('version', 'negative_count', 'float_count', 'wrong_text', 'missing_provenance'):
            data = json.loads(original)
            if mutation == 'version':
                data['format_version'] = 99
            elif mutation == 'negative_count':
                data['covenants']['Example']['activation_count'] = -1
            elif mutation == 'float_count':
                data['covenants']['Example']['activation_count'] = 2.0
            elif mutation == 'wrong_text':
                data['covenants']['Example']['effect_source_text'] = 'Wrong'
            else:
                del data['source']['acquired_at']
            path.write_text(json.dumps(data), encoding='utf-8')
            (directory / 'snapshot.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest())
            self.query(success=False)
        path.write_bytes(original)
        (directory / 'snapshot.sha256').write_text(hashlib.sha256(original).hexdigest())
        (directory / 'source.html').write_text('damaged')
        self.query(success=False)

    def test_repeated_builds_are_deterministic_and_never_replace_history(self):
        self.build()
        self.build('two')
        first, second = self.query(), self.query(snapshot='two')
        first.pop('snapshot_id')
        second.pop('snapshot_id')
        self.assertEqual(first, second)
        self.build(success=False)
        self.assertEqual(self.query()['data']['activation_count'], 2)

    def test_missing_identifier_is_distinct_from_incomplete_and_unsupported_archive(self):
        missing = self.query(snapshot='missing', success=False)
        self.assertIn('snapshot_id not found: missing', missing['message'])
        self.build()
        directory = self.root / 'store' / 'one'
        path = directory / 'snapshot.json'
        data = json.loads(path.read_bytes())
        path.write_text('corrupt bytes', encoding='utf-8')
        damaged = self.query(success=False)
        self.assertIn('snapshot SHA256 mismatch', damaged['message'])
        data['format_version'] = 99
        path.write_text(json.dumps(data), encoding='utf-8')
        (directory / 'snapshot.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest())
        unsupported = self.query(success=False)
        self.assertIn('unsupported snapshot format_version', unsupported['message'])
        path.unlink()
        corrupt = self.query(success=False)
        self.assertIn('incomplete snapshot archive: one', corrupt['message'])
        self.assertNotEqual(missing['message'], corrupt['message'])

    def test_concurrent_publishers_leave_one_complete_immutable_snapshot(self):
        command = [sys.executable, '-X', 'utf8', str(CLI), 'build', '--source', str(self.source),
                   '--manifest', str(self.manifest), '--store', str(self.root / 'store'),
                   '--snapshot-id', 'one']
        processes = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True, encoding='utf-8') for _ in range(2)]
        try:
            outputs = [process.communicate(timeout=30) for process in processes]
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
        self.assertEqual(sorted(process.returncode for process in processes), [0, 1], outputs)
        statuses = [json.loads(output[0])['status'] for output in outputs]
        self.assertEqual(sorted(statuses), ['built', 'error'])
        self.assertEqual(self.query()['data']['effect_source_text'], 'First +10%\nSecond <condition>.')
        old = self.query()
        self.build(success=False)
        self.assertEqual(self.query(), old)

    def test_interrupted_stage_is_not_queryable_and_does_not_block_rebuild(self):
        self.build()
        old = self.query()
        # Filesystem fault fixture: a killed writer may leave an incomplete stage.
        # This does not claim to interrupt at a particular instruction or prove power-loss durability.
        pending = self.root / 'store' / '.pending-interrupted'
        pending.mkdir()
        (pending / 'snapshot.json').write_text('{incomplete', encoding='utf-8')
        failed = self.query(snapshot='.pending-interrupted', success=False)
        self.assertIn('invalid snapshot_id', failed['message'])
        self.build('two')
        self.assertEqual(self.query(), old)
        self.assertEqual(self.query(snapshot='two')['data'], old['data'])
        command = [sys.executable, '-X', 'utf8', str(CLI), 'query', '--store',
                   str(self.root / 'store'), '--kind', 'covenant', '--name', 'Example']
        unspecified = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(unspecified.returncode, 2)
        self.assertIn('--snapshot-id', unspecified.stderr)


class OperatorKnowledgeCLI(unittest.TestCase):
    save_manifest = KnowledgeCLI.save_manifest
    cli = KnowledgeCLI.cli
    build = KnowledgeCLI.build
    query = KnowledgeCLI.query
    def setUp(self):
        KnowledgeCLI.setUp(self)
        self.config['operators'] = ['Operator A']
        self.source.write_text(HTML + ROSTER, encoding='utf-8')
        self.refresh_source()

    def refresh_source(self):
        self.config['source']['sha256'] = hashlib.sha256(self.source.read_bytes()).hexdigest()
        self.save_manifest()

    def operator(self, name='Operator A', snapshot='one', success=True):
        return self.cli('query', '--store', self.root / 'store', '--snapshot-id', snapshot,
                        '--kind', 'operator', '--name', name, success=success)

    def test_operator_query_preserves_membership_and_ordered_traits(self):
        self.build()
        result = self.operator()
        self.assertEqual(result['data']['name'], 'Operator A')
        self.assertEqual([m['name'] for m in result['data']['memberships']], ['Example', 'Outside'])
        self.assertEqual([m['effect_status'] for m in result['data']['memberships']],
                         ['found', 'not_in_sample'])
        self.assertEqual(result['data']['memberships'][0]['covenant']['activation_count'], 2)
        self.assertNotIn('covenant', result['data']['memberships'][1])
        self.assertEqual(result['data']['trait']['type_source_text'], 'Equipment')
        self.assertEqual(result['data']['trait']['segments'],
                         [{'index_1based': 1, 'source_text': 'Receive one tool'},
                          {'index_1based': 2, 'source_text': 'Receive two tools'}])
        self.assertIn('locator', result['data']['trait'])
        self.assertEqual([m['name'] for m in self.query()['data']['members']], ['Operator A'])
        self.assertFalse(self.query()['data']['members_complete'])
        self.assertEqual(self.operator('Unknown')['status'], 'not_in_sample')

    def test_candidates_are_source_linked_names_including_uncollected_effects(self):
        self.build()
        def candidates(kind):
            return self.cli('candidates', '--kind', kind, '--store', self.root / 'store',
                            '--snapshot-id', 'one')
        covenants = candidates('covenant')
        self.assertEqual(covenants['snapshot_id'], 'one')
        self.assertEqual([(c['name'], c['effect_status']) for c in covenants['data']],
                         [('Example', 'found'), ('Outside', 'not_in_sample')])
        self.assertTrue(all(c['locators'] for c in covenants['data']))
        self.assertFalse(covenants['coverage']['season_complete'])
        operators = candidates('operator')
        self.assertEqual([c['name'] for c in operators['data']], ['Operator A'])
        self.assertEqual(operators['source']['page_revision'], 7)

    def test_rejects_duplicate_operator_identifiers(self):
        self.config['operators'] = ['Operator A', 'Operator A']
        self.save_manifest()
        result = self.build(success=False)
        self.assertIn('duplicate operator', result['message'])
        self.operator(success=False)

    def test_nested_roster_tables_do_not_duplicate_operator_rows(self):
        self.source.write_text(HTML.replace('<table>', '<table class="wikitable mw-collapsible mw-collapsed">') + '<table class="wikitable mw-collapsible mw-collapsed">'
                               '<tbody><tr><td>' + ROSTER + '</td></tr></tbody></table>',
                               encoding='utf-8')
        self.refresh_source()
        self.build()
        self.assertEqual(self.operator()['data']['name'], 'Operator A')

    def test_rejects_conflicting_or_incomplete_relations_without_publishing(self):
        self.build()
        invalid_sources = [
            HTML.replace('<a title="Operator A">A</a>', '') + ROSTER,
            HTML + ROSTER.replace('Example<br>Outside', 'Other<br>Outside'),
            HTML.replace('<a title="Operator A">A</a>', '<a title="Operator A">A</a>' * 2) + ROSTER,
            HTML + ROSTER + ROSTER,
            HTML + ROSTER.replace('Example<br>Outside', ''),
            HTML + ROSTER.replace('Example<br>Outside', 'Example<br>Example'),
            HTML + ROSTER.replace('<div style="position:absolute">Equipment</div>', ''),
            HTML + ROSTER.replace('Receive two tools', ''),
            HTML + ROSTER.replace('Receive two tools', '<span class="mdi-arrow-right"></span>Extra'),
            HTML + ROSTER.replace('<td>Example<br>Outside</td>', ''),
            HTML + ROSTER.replace('<th>特质</th>', '<th>Unknown</th>')]
        for index, html in enumerate(invalid_sources):
            with self.subTest(index=index):
                self.source.write_text(html, encoding='utf-8')
                self.refresh_source()
                self.build('invalid', success=False)
                self.operator(snapshot='invalid', success=False)
        self.assertEqual(self.operator()['data']['trait']['type_source_text'], 'Equipment')

    def test_extended_snapshots_are_deterministic_and_reject_corrupt_relations(self):
        self.build()
        self.build('two')
        first, second = self.operator(), self.operator(snapshot='two')
        first.pop('snapshot_id')
        second.pop('snapshot_id')
        self.assertEqual(first, second)
        directory = self.root / 'store' / 'one'
        path = directory / 'snapshot.json'
        data = json.loads(path.read_bytes())
        data['operators']['Operator A']['memberships'][0]['name'] = 'Other'
        path.write_text(json.dumps(data), encoding='utf-8')
        (directory / 'snapshot.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest())
        self.operator(success=False)

    def test_historical_operator_archive_preserves_provenance_and_checks_capability(self):
        self.build()
        directory = self.root / 'store' / 'one'
        path = directory / 'snapshot.json'
        data = json.loads(path.read_bytes())
        data['extractor']['python'] = '3.13.6'
        data['extractor']['beautifulsoup4'] = '4.14.2'
        def write_archive():
            path.write_text(json.dumps(data), encoding='utf-8')
            (directory / 'snapshot.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest())
        write_archive()
        result = self.operator()
        self.assertEqual(result['extractor'], data['extractor'])
        self.assertEqual(result['data']['memberships'][0]['covenant']['activation_count'], 2)
        data['operators']['Operator A']['trait']['segments'][0]['source_text'] = 'Tampered'
        write_archive()
        self.assertIn('mismatch', self.operator(success=False)['message'])
        data['extractor']['capability'] = 'operators-99'
        write_archive()
        self.assertIn('unsupported extractor capability', self.operator(success=False)['message'])
        del data['extractor']['python']
        write_archive()
        self.assertIn('invalid extractor metadata schema', self.operator(success=False)['message'])

    def test_ambiguous_operator_identity_is_rejected(self):
        self.config['operators'].append('Operator B')
        html = HTML.replace('<a title="Operator A">A</a>',
                            '<a title="Operator A">A</a><a title="Operator B">B</a>')
        html += ROSTER.replace('<a title="Operator A">A</a>',
                               '<a title="Operator A">A</a><a title="Operator B">B</a>')
        self.source.write_text(html, encoding='utf-8')
        self.refresh_source()
        self.build(success=False)

    def test_single_trait_segment_is_kept_without_inferred_tier(self):
        self.source.write_text(HTML + ROSTER.replace(
            '<span class="mdi-arrow-right"></span><p>Receive two tools</p>', ''), encoding='utf-8')
        self.refresh_source()
        self.build()
        trait = self.operator()['data']['trait']
        self.assertEqual(trait['segments'], [{'index_1based': 1, 'source_text': 'Receive one tool'}])

    def test_invalid_operator_manifest_names_are_rejected(self):
        for names in [None, 'Operator A', [1], [''], [' Operator A']]:
            with self.subTest(names=names):
                self.config['operators'] = names
                self.save_manifest()
                self.build(success=False)

    def test_extended_covenant_rows_require_single_cells(self):
        self.source.write_text(HTML.replace('<tr><td><b>Example</b>',
                                          '<tr><td>Unexpected</td><td><b>Example</b>') + ROSTER,
                               encoding='utf-8')
        self.refresh_source()
        self.build(success=False)

    def test_full_rebuild_adds_modifies_and_removes_without_changing_history(self):
        # Two independent synthetic source sets, never merged with archived records.
        retired = HTML.replace('<script>"wgRevisionId":7</script>', '').replace(
            'Example', 'Retired').replace('Operator A', 'Retired Operator')
        retired_roster = ROSTER.replace('Operator A', 'Retired Operator').replace(
            'Example<br>Outside', 'Retired')
        self.source.write_text(HTML + retired + ROSTER + retired_roster, encoding='utf-8')
        self.config['covenants'] = ['Example', 'Retired']
        self.config['operators'] = ['Operator A', 'Retired Operator']
        self.config['applicability'] = {'season': 'Synthetic old', 'sequence': 'I', 'client_build': None}
        self.refresh_source()
        self.build()
        old_covenant = self.query()
        old_operator = self.operator()
        old_retired = self.operator('Retired Operator')
        old_candidates = self.cli('candidates', '--kind', 'covenant', '--store',
                                  self.root / 'store', '--snapshot-id', 'one')

        added = HTML.replace('<script>"wgRevisionId":7</script>', '').replace(
            'Example', 'Added').replace('Operator A', 'Added Operator')
        added_roster = ROSTER.replace('Operator A', 'Added Operator').replace(
            'Example<br>Outside', 'Added')
        newer = (HTML.replace('First', 'Revised') + added +
                 ROSTER.replace('Receive one tool', 'Receive revised tool') + added_roster)
        self.source.write_text(newer.replace('"wgRevisionId":7', '"wgRevisionId":8'), encoding='utf-8')
        self.config['covenants'] = ['Example', 'Added']
        self.config['operators'] = ['Operator A', 'Added Operator']
        self.config['applicability'] = {'season': 'Synthetic new', 'sequence': 'II', 'client_build': None}
        self.config['source']['page_revision'] = 8
        self.config['source']['acquired_at'] = '2026-11-16T12:00:00Z'
        self.refresh_source()
        self.build('two')
        new_covenant = self.query(snapshot='two')
        self.assertEqual(new_covenant['data']['effect_source_text'], 'Revised +10%\nSecond <condition>.')
        self.assertEqual(new_covenant['coverage']['covenants'], ['Added', 'Example'])
        self.assertEqual(new_covenant['coverage']['operators'], ['Added Operator', 'Operator A'])
        self.assertEqual(new_covenant['source']['page_revision'], 8)
        self.assertEqual(new_covenant['applicability'],
                         {'season': 'Synthetic new', 'sequence': 'II', 'client_build': None})
        self.assertNotEqual(old_covenant['source']['sha256'], new_covenant['source']['sha256'])
        self.assertEqual(self.query('Added', 'two')['status'], 'found')
        self.assertEqual(self.operator('Added Operator', 'two')['status'], 'found')
        self.assertEqual(self.operator(snapshot='two')['data']['trait']['segments'][0]['source_text'],
                         'Receive revised tool')
        self.assertEqual(self.query('Retired', 'two')['status'], 'not_in_sample')
        self.assertEqual(self.operator('Retired Operator', 'two')['status'], 'not_in_sample')
        self.assertEqual(self.query('Added')['status'], 'not_in_sample')
        self.assertEqual(self.operator('Added Operator')['status'], 'not_in_sample')
        for kind, expected in [('covenant', ['Added', 'Example', 'Outside']),
                               ('operator', ['Added Operator', 'Operator A'])]:
            candidates = self.cli('candidates', '--kind', kind, '--store', self.root / 'store',
                                  '--snapshot-id', 'two')
            self.assertEqual([item['name'] for item in candidates['data']], expected)
        self.assertEqual(self.query(), old_covenant)
        self.assertEqual(self.operator(), old_operator)
        self.assertEqual(self.operator('Retired Operator'), old_retired)
        self.assertEqual(self.cli('candidates', '--kind', 'covenant', '--store',
                                 self.root / 'store', '--snapshot-id', 'one'), old_candidates)
        duplicate = self.build(success=False)
        self.assertIn('snapshot_id already exists', duplicate['message'])
        self.assertEqual(self.query(), old_covenant)

    def test_failed_rebuild_does_not_publish_or_change_previous_results(self):
        self.build()
        old = self.operator()
        for html, refresh, error in [
                (HTML + ROSTER.replace('Receive one tool', 'Changed'), False, 'SHA256 mismatch'),
                (HTML + ROSTER.replace('Example<br>Outside', 'Other'), True, 'membership conflict')]:
            with self.subTest(error=error):
                self.source.write_text(html, encoding='utf-8')
                if refresh:
                    self.refresh_source()
                failed = self.build('failed', success=False)
                self.assertIn(error, failed['message'])
                self.assertIn('snapshot_id not found', self.operator(snapshot='failed', success=False)['message'])
                self.assertEqual(self.operator(), old)


ROSTER = '<table class="wikitable mw-collapsible mw-collapsed"><tbody>\n<tr><th>Tier test</th></tr><tr><th>Name</th><th>盟约</th><th>特质</th></tr>\n<tr><td><a title="Operator A">A</a></td><td>Example<br>Outside</td>\n<td><div style="position:absolute">Equipment</div>Receive one tool<span class="mdi-arrow-right"></span><p>Receive two tools</p></td></tr>\n</tbody></table>'


if __name__ == '__main__':
    unittest.main()
