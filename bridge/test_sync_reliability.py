import unittest
from copy import deepcopy
from codex_provider import read_managed_quota,ProviderError
from sync_policy import display_snapshot,unavailable_snapshot,retry_delay

def sample():
    return {'provider':'CODEX','plan':'TEAM','status':'live','sampled_at':10000,
            'rows':[{'label':'5H','remaining':8,'reset':11000,'detail':'-'},
                    {'label':'WEEKLY','remaining':80,'reset':90000,'detail':'-'}]}

class FreshnessTests(unittest.TestCase):
    def test_yesterdays_eight_percent_is_not_replayed(self):
        result=display_snapshot(sample(),now=60000)
        self.assertTrue(all(row['remaining'] is None for row in result['rows']))
        self.assertEqual(result['sampled_at'],10000)
        self.assertEqual(result['status'],'error')

    def test_failed_lookup_hides_even_recent_percentages(self):
        raw=sample();raw['status']='error'
        self.assertTrue(all(row['remaining'] is None for row in display_snapshot(raw,now=10010)['rows']))

    def test_reset_only_invalidates_expired_window(self):
        raw=sample();raw['sampled_at']=10500
        result=display_snapshot(raw,now=11000)
        self.assertIsNone(result['rows'][0]['remaining'])
        self.assertEqual(result['rows'][1]['remaining'],80)

    def test_a_true_zero_is_preserved(self):
        raw=sample();raw['rows'][0]['remaining']=0
        self.assertEqual(display_snapshot(raw,now=10010)['rows'][0]['remaining'],0)

    def test_connection_starts_in_sync_with_no_numbers(self):
        result=unavailable_snapshot(sample(),'codex','sync')
        self.assertEqual(result['status'],'sync')
        self.assertTrue(all(row['remaining'] is None for row in result['rows']))

    def test_clock_far_in_future_is_not_fresh(self):
        self.assertTrue(all(row['remaining'] is None for row in display_snapshot(sample(),now=9000)['rows']))

    def test_failure_does_not_mutate_success_cache(self):
        raw=sample();before=deepcopy(raw)
        unavailable_snapshot(raw,'codex','auth')
        self.assertEqual(raw,before)

    def test_error_retries_are_faster_than_regular_poll(self):
        self.assertEqual([retry_delay(n) for n in range(1,7)],[15,30,60,120,120,120])

class LoginTests(unittest.TestCase):
    def test_cold_process_loads_account_before_quota(self):
        calls=[]
        def request(i,method,params=None):
            calls.append(method)
            return {'account':{'type':'chatgpt'}} if method=='account/read' else {'rateLimits':{}}
        read_managed_quota(request)
        self.assertEqual(calls,['account/read','account/rateLimits/read'])

    def test_managed_auth_failure_refreshes_once_then_recovers(self):
        calls=[]
        def request(i,method,params=None):
            calls.append((method,params))
            if i==3: raise ProviderError('auth','auth_required')
            return {'account':{'type':'chatgpt'}} if method=='account/read' else {'recovered':True}
        self.assertTrue(read_managed_quota(request)['recovered'])
        self.assertEqual(calls[2],('account/read',{'refreshToken':True}))

    def test_logged_out_is_explicit_not_eight_percent(self):
        with self.assertRaises(ProviderError) as raised:
            read_managed_quota(lambda *args:{'account':None})
        self.assertEqual(raised.exception.reason,'auth_required')

    def test_api_key_is_not_treated_as_chatgpt_subscription(self):
        with self.assertRaises(ProviderError) as raised:
            read_managed_quota(lambda *args:{'account':{'type':'apiKey'}})
        self.assertEqual(raised.exception.reason,'wrong_auth_mode')

    def test_network_failure_does_not_trigger_token_refresh(self):
        calls=[]
        def request(i,method,params=None):
            calls.append(method)
            if method=='account/read': return {'account':{'type':'chatgpt'}}
            raise ProviderError('timeout','request_timeout')
        with self.assertRaises(ProviderError): read_managed_quota(request)
        self.assertEqual(calls,['account/read','account/rateLimits/read'])

    def test_repeated_auth_failure_is_bounded(self):
        calls=[]
        def request(i,method,params=None):
            calls.append(method)
            if method=='account/read': return {'account':{'type':'chatgpt'}}
            raise ProviderError('auth','auth_required')
        with self.assertRaises(ProviderError):read_managed_quota(request)
        self.assertEqual(calls.count('account/read'),2)
        self.assertEqual(calls.count('account/rateLimits/read'),2)

if __name__=='__main__':unittest.main()
