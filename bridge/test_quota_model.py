import unittest
from quota_model import codex_snapshot,volc_snapshot,openrouter_snapshot,wire_packet,remaining

class QuotaModelTests(unittest.TestCase):
    def test_missing_usage_is_unknown(self):
        data=codex_snapshot({'rateLimits':{'primary':{'windowDurationMins':300}}},now=100)
        self.assertIsNone(data['rows'][0]['remaining'])
        self.assertIn('|-1|',wire_packet(data,1,sent_at=100))
    def test_empty_window_is_not_full_quota(self):
        with self.assertRaises(ValueError): codex_snapshot({'rateLimits':{}},now=100)
    def test_window_labels_use_returned_durations(self):
        data=codex_snapshot({'rateLimits':{'primary':{'usedPercent':23,'windowDurationMins':300},'secondary':{'usedPercent':0,'windowDurationMins':10080}}},now=100)
        self.assertEqual([(r['label'],r['remaining']) for r in data['rows']],[('5H',77),('WEEKLY',100)])
    def test_codex_base_bucket_is_preferred(self):
        data=codex_snapshot({'rateLimits':{'primary':{'usedPercent':99}},'rateLimitsByLimitId':{'codex':{'primary':{'usedPercent':10,'windowDurationMins':300}}}},now=100)
        self.assertEqual(data['rows'][0]['remaining'],90)
    def test_unrelated_bucket_is_not_codex(self):
        with self.assertRaises(ValueError): codex_snapshot({'rateLimits':{'limitId':'other','primary':{'usedPercent':1}}})
    def test_nan_is_unknown_and_out_of_range_is_clamped(self):
        self.assertIsNone(remaining(float('nan')))
        self.assertEqual(remaining(150),0)
        self.assertEqual(remaining(-10),100)
    def test_volc_afp_is_not_tokens(self):
        result=volc_snapshot({'items':[{'product':'agent-plan','subscribed':True,'periods':[{'label':'5h','used':25,'total':100,'reset_at':'2026-10-19T16:00:00+08:00'}]}]},now=100)
        self.assertEqual(result['rows'][0]['remaining'],75)
        self.assertEqual(result['rows'][0]['detail'],'75 AFP LEFT')
        self.assertGreater(result['rows'][0]['reset'],0)
    def test_openrouter_money_and_zero_credit_are_distinct(self):
        row=openrouter_snapshot({'data':{'total_credits':20,'total_usage':5}},now=100)['rows'][0]
        self.assertEqual(row['remaining'],75)
        self.assertEqual(row['detail'],'USD 15.00 LEFT')
        row=openrouter_snapshot({'data':{'total_credits':0,'total_usage':0}},now=100)['rows'][0]
        self.assertIsNone(row['remaining'])
    def test_source_time_is_not_replaced_by_transport_time(self):
        packet=wire_packet({'provider':'CODEX','status':'error','sampled_at':100,'rows':[]},2,sent_at=400)
        self.assertTrue(packet.startswith('Q1|2|400|100|'))
    def test_wire_delimiters_are_removed_from_labels(self):
        packet=wire_packet({'provider':'A|B\nC','sampled_at':10,'rows':[]},1,sent_at=10)
        self.assertEqual(len(packet.rstrip().split('|')),8)

if __name__=='__main__': unittest.main()
