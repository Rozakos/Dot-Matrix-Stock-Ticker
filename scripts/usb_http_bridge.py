"""Expose the bench firmware's real HTTP handlers on localhost over USB.

Requires the esp32-oled-test firmware and pyserial. Production firmware omits
this diagnostic command. This never opens a network listener beyond localhost.
python scripts/usb_http_bridge.py COM5 --port 18080
"""
import argparse
import json
import queue
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import serial

parser = argparse.ArgumentParser()
parser.add_argument('device')
parser.add_argument('--port', type=int, default=18080)
args = parser.parse_args()
uart = serial.Serial(args.device, 115200, timeout=.5)
uart.dtr = False
uart.rts = False
responses = queue.Queue()
lock = threading.Lock()

def read_serial():
    while uart.is_open:
        try:
            frame = json.loads(uart.readline())
        except (ValueError, UnicodeError):
            continue
        if isinstance(frame, dict) and 'http_response' in frame:
            responses.put(frame['http_response'])

threading.Thread(target=read_serial, daemon=True).start()

class Handler(BaseHTTPRequestHandler):
    def handle_request(self):
        with lock:
            while not responses.empty(): responses.get_nowait()
            size = int(self.headers.get('Content-Length', '0'))
            if size > 1024:
                self.send_error(413)
                return
            body = json.loads(self.rfile.read(size)) if size else None
            frame = {'http': {'method': self.command, 'path': self.path, 'body': body}}
            uart.write(json.dumps(frame, separators=(',', ':')).encode() + b'\n')
            try:
                response = responses.get(timeout=10)
            except queue.Empty:
                self.send_error(504, 'Device did not answer over USB')
                return
        payload = json.dumps(response['body']).encode()
        self.send_response(response['status'])
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    do_GET = handle_request
    do_POST = handle_request

print(f'USB HTTP bridge: http://127.0.0.1:{args.port}', flush=True)
try:
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
finally:
    uart.close()
