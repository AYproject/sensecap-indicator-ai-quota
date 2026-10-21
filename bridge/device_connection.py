"""Find the paired USB display without assuming Windows keeps its COM number."""
import json
from pathlib import Path

class DeviceUnavailable(RuntimeError):
    pass

def identity(port):
    return {'port': port.device, 'vid': port.vid, 'pid': port.pid,
            'serial': port.serial_number or '', 'location': port.location or ''}

def select_port(ports, preferred, saved=None):
    saved = saved or {}
    vid, pid = saved.get('vid', 0x1a86), saved.get('pid', 0x7523)
    candidates = [p for p in ports if p.vid == vid and p.pid == pid]
    serial_number = saved.get('serial')
    if serial_number:
        matches = [p for p in candidates if p.serial_number == serial_number]
        return matches[0] if len(matches) == 1 else None
    if saved.get('location'):
        matches = [p for p in candidates if p.location == saved['location']]
        if len(matches) == 1: return matches[0]
    # Without a USB serial number, a reused COM number alone is not identity.
    # A unique matching CH340 may be probed for our firmware's STATUS signature.
    if len(candidates) == 1: return candidates[0]
    return None

def load_identity(path):
    try:
        value = json.loads(Path(path).read_text(encoding='utf-8-sig'))
        return value if isinstance(value, dict) else None
    except (OSError, ValueError):
        return None

def remember(path, port):
    Path(path).write_text(json.dumps(identity(port), indent=2), encoding='utf-8')
