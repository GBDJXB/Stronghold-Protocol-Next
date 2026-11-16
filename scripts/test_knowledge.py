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


if __name__ == '__main__':
    unittest.main()
