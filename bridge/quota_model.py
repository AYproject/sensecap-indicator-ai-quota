"""Provider-independent, non-secret quota snapshots and the USB wire format."""
import math
import time
from datetime import datetime

def number(value):
    if isinstance(value,bool): return None
    try: result=float(value)
    except (ValueError,TypeError): return None
    return result if math.isfinite(result) else None

def remaining(used_percent):
    used=number(used_percent)
    return None if used is None else max(0,min(100,100-used))

def reset_epoch(value):
    numeric=number(value)
    if numeric is not None:
        return int(numeric/1000 if numeric>1e12 else numeric) if numeric>0 else 0
    if isinstance(value,str):
        try:
            parsed=datetime.fromisoformat(value.replace('Z','+00:00'))
            return int(parsed.timestamp()) if parsed.tzinfo else 0
        except ValueError: pass
    return 0

def codex_snapshot(raw,now=None):
    buckets=raw.get('rateLimitsByLimitId')
    bucket=(buckets or {}).get('codex') if isinstance(buckets,dict) else None
    if not bucket:
        bucket=raw.get('rateLimits')
    if not isinstance(bucket,dict): raise ValueError('No Codex quota returned')
    # Never mistake another provider/model bucket for the Codex base bucket.
    if bucket.get('limitId') not in ('codex',None): raise ValueError('No Codex base quota returned')
    rows=[]
    for key in ('primary','secondary'):
        window=bucket.get(key)
        if not isinstance(window,dict): continue
        mins=number(window.get('windowDurationMins'))
        label='WEEKLY' if mins==10080 else ('MONTHLY' if mins and mins>=40000 else (f'{mins/60:g}H' if mins and mins>=60 else (f'{mins:g}M' if mins else key.upper())))
        rows.append({'label':label,'remaining':remaining(window.get('usedPercent')),
                     'reset':reset_epoch(window.get('resetsAt')),'detail':'-'})
    if not rows: raise ValueError('Codex did not return quota windows')
    return {'provider':'CODEX','plan':str(bucket.get('planType') or 'CHATGPT PLAN').upper(),
            'status':'live','sampled_at':int(now or time.time()),'rows':rows[:3]}

def volc_snapshot(raw,now=None):
    if isinstance(raw.get('data'),dict): raw=raw['data']
    items=raw.get('items',[])
    item=next((x for x in items if x.get('product')=='agent-plan' and x.get('subscribed') and not x.get('error')),None)
    if not item: raise ValueError('No active Agent Plan quota for this identity')
    rows=[]
    for p in item.get('periods',[]):
        used,total=number(p.get('used')),number(p.get('total'))
        percent=p.get('percent')
        if percent is None and used is not None and total is not None and total>0:
            percent=used/total*100
        detail=f'{max(0,total-used):,.0f} AFP LEFT' if total is not None and used is not None and total>0 else '-'
        rows.append({'label':str(p.get('label','WINDOW')).upper(),'remaining':remaining(percent),
                     'reset':reset_epoch(p.get('reset_at')),'detail':detail})
    if not rows: raise ValueError('Agent Plan did not return quota periods')
    return {'provider':'VOLCENGINE','plan':('AGENT PLAN '+str(item.get('tier') or '')).strip().upper(),
            'status':'live','sampled_at':int(now or time.time()),'rows':rows[:3]}

def openrouter_snapshot(raw,now=None):
    data=raw.get('data',{})
    total,used=number(data.get('total_credits')),number(data.get('total_usage'))
    if total is None or used is None or total<0 or used<0: raise ValueError('OpenRouter credits response missing totals')
    left=max(0,total-used)
    percent=max(0,min(100,left/total*100)) if total>0 else None
    return {'provider':'OPENROUTER','plan':'ACCOUNT CREDITS','status':'live','sampled_at':int(now or time.time()),
            'rows':[{'label':'CREDITS','remaining':percent,'reset':0,'detail':f'USD {left:.2f} LEFT'}]}

def text_field(value,maxlen=36):
    text=str(value).encode('ascii',errors='replace').decode('ascii')
    text=''.join(c for c in text if c not in '|\r\n' and ord(c)>=32).strip()
    return text[:maxlen] or '-'

def wire_packet(snapshot,sequence,sent_at=None):
    rows=snapshot.get('rows',[])[:3]
    fields=['Q1',str(sequence),str(int(sent_at or time.time())),str(snapshot.get('sampled_at') or 0),
            text_field(snapshot.get('provider','CODEX'),16),text_field(snapshot.get('plan','-'),24),
            text_field(snapshot.get('status','error'),12),str(len(rows))]
    for row in rows:
        value=number(row.get('remaining'))
        fields.extend([text_field(row.get('label','WINDOW'),16),
                       f'{max(0,min(100,value)):.3f}' if value is not None else '-1',
                       str(reset_epoch(row.get('reset'))),text_field(row.get('detail','-'),36)])
    return '|'.join(fields)+'\n'
