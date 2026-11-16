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


ROSTER = '<table class="wikitable mw-collapsible mw-collapsed"><tbody>\n<tr><th>Tier test</th></tr><tr><th>Name</th><th>盟约</th><th>特质</th></tr>\n<tr><td><a title="Operator A">A</a></td><td>Example<br>Outside</td>\n<td><div style="position:absolute">Equipment</div>Receive one tool<span class="mdi-arrow-right"></span><p>Receive two tools</p></td></tr>\n</tbody></table>'


if __name__ == '__main__':
    unittest.main()
