"""Release selection, bounded edits and safe MediaWiki publication."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch
from contextlib import redirect_stdout
import io

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('wiki_sync', ROOT / 'tools/release/sync_wiki.py')
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


def release(tag='v0.11.4.0', **kwargs):
    return dict(tag_name=tag, published_at='2026-10-22T08:29:36Z', draft=False, prerelease=False, **kwargs)


class WikiSyncTest(unittest.TestCase):
    def test_paginate_and_filter_even_when_first_page_has_no_game_releases(self):
        stable = release()
        preview = release('v0.11.2.0'); preview['prerelease'] = True
        draft = release('v0.11.5.0'); draft['draft'] = True
        fetch = Mock(side_effect=[[release('evidence-test'), draft], [preview, stable], [], stable])
        rows, latest = sync.releases(fetch)
        self.assertEqual({r['tag_name'] for r in rows}, {'v0.11.4.0', 'v0.11.2.0'})
        self.assertEqual(latest, 'v0.11.4.0')
        self.assertIn('page=3', fetch.call_args_list[2].args[0])

    def test_latest_comes_from_github_not_publication_order(self):
        stable = release(); older = release('v0.11.0.0')
        rows, latest = sync.releases(Mock(side_effect=[[stable, older], [], older]))
        self.assertEqual(latest, older['tag_name'])

    def test_no_untrusted_names_or_bodies_in_wikitext(self):
        stable = release(name='</nowiki>{{malicious}}', body='[[File:bad]]')
        preview = release('v0.11.2.0'); preview['prerelease'] = True
        text = sync.render('Download and Install', [stable, preview], stable['tag_name'])
        self.assertNotIn('malicious', text)
        self.assertIn('Public previews', text)
        self.assertIn('[[Git|current repository and build instructions]]', text)
        self.assertIn('(preview;', text)
        self.assertFalse(sync.TAG.fullmatch('v0.11.4.0\n{{bad}}'))

    def test_preserve_editorial_content_and_repeat_noop(self):
        text = 'translations\n' + sync.BEGIN + '\nold\n' + sync.END + '\nhistory'
        generated = sync.BEGIN + '\nnew\n' + sync.END
        result = sync.replace_section(text, generated)
        self.assertTrue(result.startswith('translations\n'))
        self.assertTrue(result.endswith('\nhistory'))
        self.assertEqual(sync.replace_section(result, generated), result)

    def test_initialize_preserves_translations_and_history(self):
        text = '{{Translations|Article=Changes}}\n\nlegacy'
        result = sync.replace_section(text, sync.BEGIN + '\nnew\n' + sync.END, True)
        self.assertTrue(result.startswith('{{Translations|Article=Changes}}\n'))
        self.assertTrue(result.endswith('legacy'))

    def test_missing_duplicate_reversed_markers_fail(self):
        for text in ('plain', sync.END + sync.BEGIN, sync.BEGIN * 2 + sync.END):
            with self.subTest(text=text), self.assertRaises(sync.SyncError):
                sync.replace_section(text, 'replacement')

    def test_redirects_never_forward_credentials(self):
        with self.assertRaises(sync.SyncError):
            sync.NoRedirect().redirect_request(None, None, 302, '', {}, 'http://other.invalid')

    def test_login_requires_credentials_and_success(self):
        wiki = sync.Wiki(Mock())
        with self.assertRaises(sync.SyncError):
            wiki.login('', '')
        wiki.fetch = Mock(side_effect=[{'query': {'tokens': {'logintoken': 'token'}}},
                                       {'login': {'result': 'NeedToken'}}])
        with self.assertRaises(sync.SyncError):
            wiki.login('Genixpro@ReleaseSync', 'secret')

    def test_account_login_uses_clientlogin_and_never_continues_challenges(self):
        from urllib.parse import parse_qs
        for status in ('PASS', 'FAIL', 'UI', 'REDIRECT', 'RESTART'):
            fetch = Mock(side_effect=[{'query': {'tokens': {'logintoken': 'token'}}},
                                      {'clientlogin': {'status': status}}])
            wiki = sync.Wiki(fetch)
            if status == 'PASS':
                wiki.login('Genixpro', 'test-password')
            else:
                with self.assertRaises(sync.SyncError):
                    wiki.login('Genixpro', 'test-password')
            self.assertEqual(fetch.call_count, 2)
            params = parse_qs(fetch.call_args.args[1].decode())
            self.assertEqual(params['action'], ['clientlogin'])
            self.assertEqual(params['username'], ['Genixpro'])
            self.assertEqual(params['loginreturnurl'], ['https://globulation2.org/wiki/Main_Page'])

    def test_edit_conflict_captcha_and_session_loss_fail(self):
        page = dict(title='Changes', revid=5, timestamp='time', start='start')
        for result in ({'error': {'code': 'editconflict'}}, {'error': {'code': 'assertuserfailed'}},
                       {'edit': {'result': 'Failure', 'captcha': {}}}):
            with self.subTest(result=result), self.assertRaises(sync.SyncError):
                sync.Wiki(Mock(return_value=result)).save(page, 'text', 'secret')
        fetch = Mock(return_value={'edit': {'result': 'Success', 'newrevid': 6}})
        self.assertEqual(sync.Wiki(fetch).save(page, 'text', 'secret'), 6)
        from urllib.parse import parse_qs
        params = parse_qs(fetch.call_args.kwargs.get('data', fetch.call_args.args[1]).decode())
        self.assertEqual(params['baserevid'], ['5'])
        self.assertEqual(params['assert'], ['user'])
        self.assertEqual(params['nocreate'], ['1'])

    def test_read_permissions_match_mediawiki_131_flags(self):
        base = dict(pageid=1, title='Changes', revisions=[{'*': 'text', 'revid': 5,
                    'timestamp': 'time'}])
        for actions, allowed in (([], False), ({'edit': ''}, True),
                                 ({'edit': True}, True), ({}, False)):
            page = dict(base, actions=actions)
            fetch = Mock(return_value={'query': {'pages': {'1': page}}, 'curtimestamp': 'start'})
            with self.subTest(actions=actions):
                self.assertEqual(sync.Wiki(fetch).read('Changes')['editable'], allowed)

    def test_missing_page_and_api_errors_fail(self):
        fetch = Mock(return_value={'query': {'pages': {'-1': {'title': 'Changes', 'missing': ''}}}})
        with self.assertRaises(sync.SyncError):
            sync.Wiki(fetch).read('Changes')
        with self.assertRaises(sync.SyncError):
            sync.Wiki(Mock(return_value={'error': {'code': 'permissiondenied'}})).call(action='edit')

    def test_publish_preflights_all_pages_before_writing(self):
        stable = release()
        for failure in ('permission', 'markers'):
            pages = [dict(title=t, text=sync.render(t, [stable], stable['tag_name']),
                          revid=5, timestamp='time', start='start', editable=True) for t in sync.PAGES]
            if failure == 'permission':
                pages[1]['editable'] = False
            else:
                pages[1]['text'] = 'missing markers'
            pages[0]['text'] = sync.BEGIN + '\nstale\n' + sync.END
            wiki = Mock(); wiki.read.side_effect = pages
            with self.subTest(failure=failure), patch.object(sync, 'Wiki', return_value=wiki), \
                 patch.object(sync, 'releases', return_value=([stable], stable['tag_name'])), \
                 patch.object(sync.sys, 'argv', ['sync_wiki.py', '--publish']):
                with self.assertRaises(sync.SyncError):
                    sync.main()
                wiki.save.assert_not_called()

    def test_repeat_publish_does_not_request_edit_token_or_save(self):
        stable = release()
        wiki = Mock()
        wiki.read.side_effect = [dict(title=t, text=sync.render(t, [stable], stable['tag_name']),
                                      editable=True) for t in sync.PAGES]
        with patch.object(sync, 'Wiki', return_value=wiki), \
             patch.object(sync, 'releases', return_value=([stable], stable['tag_name'])), \
             patch.object(sync.sys, 'argv', ['sync_wiki.py', '--publish']), redirect_stdout(io.StringIO()):
            sync.main()
        wiki.save.assert_not_called()
        wiki.call.assert_not_called()

    def test_workflow_has_narrow_scheduled_publication_guard(self):
        text = (ROOT / '.github/workflows/sync-wiki.yml').read_text()
        self.assertIn("cron: '23 13 * * 1'", text)
        self.assertIn("github.event_name == 'schedule' && github.actor_id == 6193625", text)
        self.assertIn("github.triggering_actor == 'genixpro'", text)
        self.assertIn('environment: wiki-publication', text)
        self.assertIn("vars.WIKI_PUBLISH_ENABLED != 'true'", text)
        self.assertIn("vars.WIKI_PUBLISH_ENABLED == 'true'", text)
        self.assertIn('!inputs.publish', text)
        self.assertIn('default: false', text)
        self.assertIn('cancel-in-progress: false', text)
        self.assertNotIn('--initialize', text)


if __name__ == '__main__':
    unittest.main()
