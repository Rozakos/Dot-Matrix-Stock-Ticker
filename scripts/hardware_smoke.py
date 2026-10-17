"""Opt-in device contract test; all edited settings are restored in finally.

python scripts/hardware_smoke.py http://192.168.1.x
"""
import json
import sys
import time
import urllib.error
import urllib.request

base = sys.argv[1].rstrip('/')

def request(path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data,
        headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)

code, info = request('/api/device-info')
assert code == 200 and info['type'] in ('oled_stock_ticker', 'dot_matrix_stock_ticker')
assert {'state', 'settings_api'} <= set(info['capabilities'])
code, original = request('/api/settings')
assert code == 200 and 'api_key' not in original and 'wifi_pass' not in original
assert original['has_api_key']
restore = {key: original[key] for key in ('symbols', 'refresh_s')}
try:
    for bad in ({'symbols': []}, {'symbols': ['NVDA', 'NVDA']},
                {'symbols': ['AAPL/../../']}, {'symbols': ['A'*13]},
                {'symbols': ['A'+str(i) for i in range(17)]},
                {'refresh_s': 14}, {'refresh_s': 3601},
                {'refresh_s': -1}, {'refresh_s': '30'}, {'api_key': None}):
        code, result = request('/api/settings', bad)
        assert code == 400 and 'error' in result, (bad, code, result)
        assert request('/api/settings')[1] == original, 'Invalid update changed settings'
    code, result = request('/api/settings', {'symbols': ['nvda', 'amd'], 'refresh_s': 30})
    assert code == 200 and result['symbols'] == ['NVDA', 'AMD']
    assert result['refresh_s'] == 30 and result['has_api_key']
    deadline = time.monotonic() + 35
    while True:
        code, state = request('/api/state')
        assert code == 200
        assert [q['symbol'] for q in state['quotes']] == ['NVDA', 'AMD']
        if all(q['price'] is not None and q['fresh'] for q in state['quotes']): break
        assert time.monotonic() < deadline, state
        time.sleep(1)
    print('PASS: identity, live quotes, write-only token, validation, native settings')
finally:
    code, saved = request('/api/settings', restore)
    assert code == 200 and all(saved[key] == value for key, value in restore.items())
    print('Original watchlist and refresh restored')
