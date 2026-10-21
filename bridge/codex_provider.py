"""Read quota via Codex's documented app-server account API.

Uses the existing Codex login. Never reads, prints or copies token files.
No model inference, new task, credit purchase or rate-limit reset is requested.
"""
import json
import os
import queue
import shutil
import subprocess
import threading
import time
from pathlib import Path


class ProviderError(RuntimeError):
    def __init__(self,message,reason='provider_unavailable',rpc_code=None):
        super().__init__(message)
        self.reason=reason
        self.rpc_code=rpc_code

def resolve_executable(explicit=None):
    explicit=explicit or os.environ.get('D1_CODEX_EXE')
    if explicit:
        found=shutil.which(explicit) or (explicit if Path(explicit).is_file() else None)
        if found: return str(found)
        raise ProviderError('Configured Codex executable was not found.','cli_not_found')
    if os.name=='nt':
        base=Path(os.environ.get('LOCALAPPDATA',str(Path.home()/'AppData/Local')))/'OpenAI/Codex/bin'
        candidates=sorted(base.glob('*/codex.exe'),key=lambda p:p.stat().st_mtime,reverse=True)
        if candidates: return str(candidates[0])
    found=shutil.which('codex')
    if found: return found
    raise ProviderError('Codex CLI not found. Install it or set D1_CODEX_EXE.','cli_not_found')

def rpc_error(error):
    # Classify server diagnostics without retaining messages that may contain identity data.
    message=str(error.get('message','')).lower()
    auth_words=('authentication','unauthorized','not logged','sign in','login','token','401')
    reason='auth_required' if any(word in message for word in auth_words) else 'rpc_unavailable'
    return ProviderError('Codex account authentication needs attention.' if reason=='auth_required'
                         else 'Codex quota service is unavailable.',reason,error.get('code'))

def read_managed_quota(request):
    # Loading account state first avoids querying before managed auth is ready on a cold process.
    profile=request(2,'account/read',{'refreshToken':False})
    account=profile.get('account') or {}
    if not account:
        raise ProviderError('Sign in to Codex with ChatGPT on this computer.','auth_required')
    if account.get('type')!='chatgpt':
        raise ProviderError('A ChatGPT account login is required for subscription quotas.','wrong_auth_mode')
    try:
        return request(3,'account/rateLimits/read')
    except ProviderError as error:
        if error.reason!='auth_required': raise
        # Only Codex refreshes its own managed credentials; no token files are read or copied.
        request(4,'account/read',{'refreshToken':True})
        return request(5,'account/rateLimits/read')


def get_codex_limits(executable=None, timeout=35):
    executable=resolve_executable(executable)
    flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    proc = subprocess.Popen([executable, 'app-server'], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, encoding='utf-8', errors='replace',
                            creationflags=flags)
    incoming = queue.Queue()

    def stdout_reader():
        for line in proc.stdout:
            try:
                incoming.put(json.loads(line))
            except json.JSONDecodeError:
                continue
        incoming.put(None)

    def stderr_drain():
        # Native app-server diagnostics are deliberately not retained/exported.
        for _line in proc.stderr:
            pass

    threading.Thread(target=stdout_reader, daemon=True).start()
    threading.Thread(target=stderr_drain, daemon=True).start()

    def send(message):
        proc.stdin.write(json.dumps(message, separators=(',',':'))+'\n')
        proc.stdin.flush()

    def request(rpc_id, method, params=None):
        message={'method':method,'id':rpc_id}
        if params is not None:
            message['params']=params
        send(message)
        deadline=time.monotonic()+timeout
        while True:
            left=deadline-time.monotonic()
            if left <= 0:
                raise ProviderError('Codex quota request timed out.','request_timeout')
            try:
                response=incoming.get(timeout=left)
            except queue.Empty as exc:
                raise ProviderError('Codex quota request timed out.','request_timeout') from exc
            if response is None:
                raise ProviderError('Codex app-server exited before returning quota.')
            if response.get('id') == rpc_id and 'method' not in response:
                if 'error' in response:
                    raise rpc_error(response['error'])
                return response.get('result',{})
            if 'method' in response and 'id' in response:
                # Managed login is supported. External token refresh needs its owner.
                send({'id':response['id'],'error':{'code':-32601,'message':'This quota-only client does not supply external credentials.'}})

    try:
        request(1,'initialize',{'clientInfo':{'name':'d1_quota_monitor','title':'D1 AI Quota Monitor','version':'0.1.0'}})
        send({'method':'initialized','params':{}})
        result=read_managed_quota(request)
        if not isinstance(result,dict):
            raise ProviderError('Invalid Codex quota response.')
        return result
    finally:
        try:
            proc.stdin.close()
            proc.wait(timeout=3)
        except (OSError,subprocess.TimeoutExpired):
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == '__main__':
    result=get_codex_limits()
    # Quota payload only: no account profile, email, credentials or conversations.
    print(json.dumps({k:result.get(k) for k in ('rateLimits','rateLimitsByLimitId')},ensure_ascii=True,indent=2))
