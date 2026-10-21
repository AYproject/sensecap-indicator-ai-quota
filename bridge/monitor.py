"""Online quota reader -> D1 USB display. Default source: current Codex login."""
from pathlib import Path
import argparse
import copy
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
import serial
from serial.tools import list_ports
from codex_provider import get_codex_limits
from quota_model import codex_snapshot,volc_snapshot,openrouter_snapshot,wire_packet
from device_connection import DeviceUnavailable,select_port,load_identity,remember
from sync_policy import display_snapshot,unavailable_snapshot,retry_delay

def source_snapshot(source):
    if source=='codex': return codex_snapshot(get_codex_limits())
    if source=='volc':
        executable=shutil.which('arkcli')
        if not executable: raise RuntimeError('Install/login arkcli before selecting Volcengine.')
        env=os.environ.copy(); env['ARKCLI_NO_UPDATE_NOTIFIER']='1'
        result=subprocess.run([executable,'usage','plan','--product','agent-plan','--format','json'],
            capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45,env=env,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        if result.returncode: raise RuntimeError('Agent Plan quota unavailable; check arkcli login.')
        return volc_snapshot(json.loads(result.stdout))
    token=os.environ.get('D1_OPENROUTER_MANAGEMENT_KEY','').strip()
    if not token: raise RuntimeError('Set D1_OPENROUTER_MANAGEMENT_KEY locally to read account credits.')
    req=urllib.request.Request('https://openrouter.ai/api/v1/credits',headers={'Authorization':'Bearer '+token,'User-Agent':'D1QuotaMonitor/0.1'})
    try:
        with urllib.request.urlopen(req,timeout=25) as response: raw=json.load(response)
    except Exception as exc:
        raise RuntimeError('OpenRouter credit lookup failed; check management-key access/network.') from None
    return openrouter_snapshot(raw)

class D1Link:
    def __init__(self,port,reset=False):
        self.connection=serial.Serial(port=None,baudrate=115200,timeout=.25,write_timeout=2)
        self.connection.dtr=False; self.connection.rts=False
        self.log=[]
        try:
            self.connection.port=port; self.connection.open()
            if reset:
                self.connection.rts=True; time.sleep(.12); self.connection.rts=False
                self.collect(4)
            else: self.collect(.6)
        except Exception:
            self.connection.close()
            raise
    def close(self):
        try: self.connection.close()
        except OSError: pass
    def send(self,line):
        self.connection.write(line.encode('ascii')); self.connection.flush()
    def read_line(self):
        line=self.connection.readline().decode('ascii',errors='replace').strip()
        if line: self.log.append(line)
        return line
    def collect(self,seconds):
        deadline=time.monotonic()+seconds
        lines=[]
        while time.monotonic()<deadline:
            line=self.read_line()
            if line: lines.append(line)
        return lines
    def wait_for(self,prefix,timeout=4):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            line=self.read_line()
            if line.startswith(prefix): return line
        raise DeviceUnavailable('D1 did not respond with '+prefix)
    def publish(self,snapshot,sequence):
        self.send(wire_packet(snapshot,sequence))
        return self.wait_for(f'ACK seq={sequence} ')
    def status(self):
        self.send('STATUS\n')
        line=self.wait_for('STATUS ')
        state=dict(re.findall(r'(\w+)=([^ ]+)',line))
        try:
            int(state['seq']); int(state['rows'])
        except (KeyError,ValueError): raise DeviceUnavailable('Incomplete device status')
        return state
    def read_exact(self,size):
        data=bytearray(); deadline=time.monotonic()+6
        while len(data)<size and time.monotonic()<deadline:
            data.extend(self.connection.read(size-len(data)))
        if len(data)!=size: raise RuntimeError('D1 frame readback truncated')
        return data
    def capture(self,path):
        from PIL import Image
        self.send('DUMP\n')
        header=self.wait_for('FRAME_RLE ',timeout=5)
        _,w,h=header.split(); w,h=int(w),int(h)
        if (w,h)!=(480,480): raise RuntimeError('Unexpected D1 framebuffer size')
        pixels=bytearray(); count=0
        while count<w*h:
            packet=self.read_exact(4)
            run=int.from_bytes(packet[:2],'big'); value=int.from_bytes(packet[2:],'big')
            if run==0 or count+run>w*h: raise RuntimeError('Invalid D1 frame data')
            rgb=bytes((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
            pixels.extend(rgb*run); count+=run
        self.collect(.3)
        path=Path(path); path.parent.mkdir(parents=True,exist_ok=True)
        Image.frombytes('RGB',(w,h),bytes(pixels)).save(path)

def verify(link,snapshot):
    results={'boot_display_ready':any('READY D1_AI_QUOTA' in s for s in link.log),
             'touch_controller_ack':any('TOUCH probe=OK' in s for s in link.log)}
    state=link.status()
    results['studio_ui']=state.get('ui')=='studio_v052'
    results['cached_frosted_material']=state.get('material')=='cached_frosted'
    results['antialiased_type']=state.get('aa')=='4'
    if snapshot['provider']=='CODEX': results['chatgpt_heading']=state.get('title')=='ChatGPT'
    expected=snapshot['rows'][0]['remaining']
    results['real_quota_matches_display']=expected is None or abs(float(state['remaining'])-expected)<.01
    if len(snapshot['rows'])>1:
        link.send('NEXT\n'); time.sleep(.2)
        next_state=link.status()
        results['window_switch']=abs(float(next_state['remaining'])-snapshot['rows'][1]['remaining'])<.01
        results['window_switch_is_partial_write']=0<int(next_state['last_pixels'])<480*480
        for _ in range(len(snapshot['rows'])-1): link.send('NEXT\n')
        time.sleep(.2)
    link.send('FREEZE\n'); link.wait_for('PHOTO frozen=1')
    frozen1=link.status(); time.sleep(1.2); frozen2=link.status()
    results['photo_freeze']=frozen1['frames']==frozen2['frames'] and frozen2['frozen']=='1'
    results['photo_mode_is_partial_write']=int(frozen1['last_pixels'])<480*480
    link.send('FREEZE\n'); link.wait_for('PHOTO frozen=0')
    resumed1=link.status(); time.sleep(1.2); resumed2=link.status()
    results['photo_resume']=resumed1['frozen']=='0' and resumed2['frozen']=='0'
    # Rendering an identical view must not copy the LCD framebuffer again.
    before=link.status()
    link.send('REDRAW\n'); link.wait_for('REDRAW done')
    after=link.status()
    results['identical_view_no_lcd_write']=before['pixels']==after['pixels'] and before['frames']==after['frames']
    results['incremental_present_enabled']=after.get('incremental')=='1'
    results['fifteen_minute_stale_threshold']=after.get('fresh_for')=='900'
    previous=link.status()
    link.send('Q1|not-a-valid-packet\n'); link.wait_for('REJECT ')
    after=link.status()
    results['malformed_packet_preserves_data']=previous['seq']==after['seq']
    results['scope']='Online Codex data, USB transport, framebuffer, window switching, photo controls and redundant-write suppression. Physical finger gestures and optical flicker require on-device observation.'
    results['passed']=all(value for value in results.values() if isinstance(value,bool))
    return results

def record(summary):
    with (ROOT/'private/monitor.log').open('a',encoding='utf-8') as file:
        file.write(json.dumps(summary)+'\n')
    print(json.dumps(summary),flush=True)

def cached_snapshot(source):
    try:
        path=ROOT/'private/last_success.json'
        if not path.exists(): path=ROOT/'private/latest_snapshot.json'
        snapshot=json.loads(path.read_text(encoding='utf-8'))
        expected={'codex':'CODEX','volc':'VOLCENGINE','openrouter':'OPENROUTER'}[source]
        if snapshot.get('provider')!=expected or not snapshot.get('sampled_at') or snapshot.get('status')!='live': return None
        wire_packet(snapshot,1)  # Validate the persisted packet shape before replay.
        return snapshot
    except (OSError,ValueError,KeyError,TypeError): return None

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',choices=['codex','volc','openrouter'],default='codex')
    parser.add_argument('--port',default='auto',help='Auto-detect the paired D1 CH340 serial port (default: auto)')
    parser.add_argument('--interval',type=int,default=300)
    parser.add_argument('--once',action='store_true')
    parser.add_argument('--read-only',action='store_true',help='Fetch online data without opening the USB port')
    parser.add_argument('--capture')
    parser.add_argument('--verify',action='store_true')
    args=parser.parse_args()
    (ROOT/'private').mkdir(exist_ok=True)
    if args.read_only:
        print(json.dumps(source_snapshot(args.source),indent=2)); return 0
    identity_path=ROOT/'private/device_identity.json'
    saved_identity=load_identity(identity_path)
    stop_path=ROOT/'private/stop.request'
    refresh_path=ROOT/'private/refresh.request'
    started_at=time.time()
    def stop_requested():
        try: return stop_path.stat().st_mtime>=started_at
        except OSError: return False
    link=None; last_good=cached_snapshot(args.source)
    sequence=0; published_sequence=0; next_poll=0; last_poll=0; next_health=0; did_capture=False
    waiting=False
    failures=0
    try:
        while not stop_requested():
            try:
                if link is None:
                    port=select_port(list_ports.comports(),args.port,saved_identity)
                    if port is None: raise DeviceUnavailable('Waiting for paired display')
                    link=D1Link(port.device,reset=args.verify)
                    state=link.status()
                    if not state.get('ui','').startswith(('modular_','studio_')):
                        raise DeviceUnavailable('USB device is not the quota display')
                    remember(identity_path,port); saved_identity=load_identity(identity_path)
                    waiting=False
                    record({'event':'device_connected','port':port.device,'updated_at':int(time.time())})
                    # Never show a disk cache as a fresh online reading on startup or reconnect.
                    sequence+=1
                    link.publish(unavailable_snapshot(last_good,args.source,'sync'),sequence)
                    published_sequence=sequence
                    # A new device connection always gets an immediate online poll.
                    next_poll=0; next_health=time.monotonic()+5
                now=time.monotonic()
                if refresh_path.exists():
                    try: refresh_path.unlink()
                    except FileNotFoundError: pass
                    record({'event':'refresh_requested','updated_at':int(time.time())})
                    next_poll=0
                if now>=next_poll:
                    last_poll=now; sequence+=1
                    try:
                        snapshot=source_snapshot(args.source)
                        last_good=copy.deepcopy(snapshot)
                        (ROOT/'private/last_success.json').write_text(json.dumps(snapshot,indent=2),encoding='utf-8')
                        failures=0
                        error=None
                        reason=None
                    except Exception as exc:
                        error=type(exc).__name__
                        reason=getattr(exc,'reason','provider_unavailable')
                        failures+=1
                        snapshot=unavailable_snapshot(last_good,args.source,
                            'auth' if reason in ('auth_required','wrong_auth_mode') else 'error')
                    if stop_requested(): break
                    snapshot=display_snapshot(snapshot)
                    ack=link.publish(snapshot,sequence); published_sequence=sequence
                    (ROOT/'private/latest_snapshot.json').write_text(json.dumps(snapshot,indent=2),encoding='utf-8')
                    summary={'updated_at':int(time.time()),'sequence':sequence,'provider':snapshot['provider'],
                             'status':snapshot['status'],'rows':snapshot['rows'],'device_ack':ack,
                             'poll_interval_seconds':max(60,args.interval)}
                    delay=retry_delay(failures) if failures else max(60,args.interval)
                    summary['next_query_in_seconds']=delay
                    summary['last_success_at']=(last_good or {}).get('sampled_at',0)
                    if error:
                        summary['error_type']=error
                        summary['error_reason']=reason
                    (ROOT/'private/health.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
                    record(summary)
                    if args.capture and not did_capture:
                        link.capture(args.capture); did_capture=True
                    if args.verify:
                        if snapshot['status']!='live': raise RuntimeError('A successful live lookup is required for validation.')
                        result=verify(link,snapshot)
                        (ROOT/'private/device_validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
                        (ROOT/'private/device_validation.log').write_text('\n'.join(link.log),encoding='utf-8')
                        print(json.dumps({'validation':result}),flush=True)
                        if not result['passed']: return 1
                    if args.once: return 0 if snapshot['status']=='live' else 1
                    next_poll=time.monotonic()+delay
                if time.monotonic()>=next_health:
                    state=link.status(); next_health=time.monotonic()+5
                    if published_sequence and int(state['seq'])!=published_sequence:
                        record({'event':'display_rebooted','updated_at':int(time.time())})
                        sequence+=1
                        link.publish(unavailable_snapshot(last_good,args.source,'sync'),sequence)
                        published_sequence=sequence
                        next_poll=0
                line=link.read_line()
                if line.startswith('READY D1_AI_QUOTA'): next_health=0
                if line=='REQUEST REFRESH' and time.monotonic()-last_poll>=5: next_poll=0
            except (serial.SerialException,OSError,DeviceUnavailable) as exc:
                if link is not None: link.close(); link=None
                if args.once or args.verify: raise
                if not waiting:
                    record({'event':'waiting_for_device','reason':type(exc).__name__,'updated_at':int(time.time())})
                    waiting=True
                for _ in range(8):
                    if stop_requested(): break
                    time.sleep(.25)
    finally:
        if link is not None: link.close()

if __name__=='__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    try: raise SystemExit(main())
    except KeyboardInterrupt: raise SystemExit(0)
    except Exception as error:
        # Provider implementations give fixed diagnostic messages, never HTTP headers or tokens.
        print(f'Monitor stopped: {type(error).__name__}: {error}',file=sys.stderr)
        raise SystemExit(1)
