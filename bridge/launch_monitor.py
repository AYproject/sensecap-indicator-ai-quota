"""Detach the monitor with explicit file handles, independent of the CMD/PowerShell window."""
from pathlib import Path
import argparse
import os
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--port',default='auto')
parser.add_argument('--source',choices=('codex','volc','openrouter'),default='codex')
parser.add_argument('--interval',type=int,default=300)
args=parser.parse_args()
private=root/'private';private.mkdir(exist_ok=True)
flags=(subprocess.CREATE_NO_WINDOW|subprocess.CREATE_NEW_PROCESS_GROUP) if os.name=='nt' else 0
with (private/'bridge.stdout.log').open('wb') as stdout, (private/'bridge.stderr.log').open('wb') as stderr:
    process=subprocess.Popen([sys.executable,str(root/'bridge/monitor.py'),'--port',args.port,
        '--source',args.source,'--interval',str(args.interval)],cwd=root,
        stdin=subprocess.DEVNULL,stdout=stdout,stderr=stderr,close_fds=True,
        creationflags=flags,start_new_session=os.name!='nt')
(private/'bridge.pid').write_text(str(process.pid),encoding='ascii')
