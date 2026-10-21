"""Freshness and failure policy: an old percentage must never appear as current quota."""
import copy
import time

MAX_AGE=900

def provider_name(source):
    return {'codex':'CODEX','volc':'VOLCENGINE','openrouter':'OPENROUTER'}[source]

def display_snapshot(snapshot,now=None):
    result=copy.deepcopy(snapshot)
    now=int(time.time() if now is None else now)
    sampled=int(result.get('sampled_at') or 0)
    current=result.get('status')=='live' and sampled>0 and -30<=now-sampled<=MAX_AGE
    for row in result.get('rows',[]):
        reset=int(row.get('reset') or 0)
        if not current or (reset and reset<=now):
            row['remaining']=None; row['detail']='-'
    if result.get('status')=='live' and not current: result['status']='error'
    return result

def unavailable_snapshot(last_good,source,state='error'):
    result=copy.deepcopy(last_good) if last_good else {
        'provider':provider_name(source),'plan':'ACCOUNT','sampled_at':0,'rows':[]}
    result['status']=state
    return display_snapshot(result)

def retry_delay(failures):
    return (15,30,60,120)[min(max(0,failures-1),3)]
