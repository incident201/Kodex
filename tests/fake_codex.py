#!/usr/bin/env python3
"""Deterministic app-server byte proxy for integration tests. No network or auth."""
import base64
import hashlib
import json
import os
import struct
import sys
import time
from pathlib import Path

validate = None
if os.environ.get('KODEX_VALIDATE_SCHEMA'):
    from jsonschema import Draft7Validator
    schema = json.loads((Path(__file__).parent.parent / 'protocol/codex-0.156.1.json').read_text())
    def validate(message):
        if 'method' in message:
            name = 'ClientRequest' if 'id' in message else 'ClientNotification'
            Draft7Validator(dict(schema, **{'$ref': '#/definitions/' + name})).validate(message)
        elif message.get('id') == 987:
            Draft7Validator(dict(schema, **{'$ref': '#/definitions/ToolRequestUserInputResponse'})).validate(message['result'])

args = sys.argv[1:]
if args == ['app-server', 'daemon', 'start']:
    sys.exit(0)
if args == ['app-server', 'daemon', 'version']:
    print(json.dumps({'cliVersion': '0.156.1', 'appServerVersion': '0.156.1'}))
    sys.exit(0)
if args != ['app-server', 'proxy']:
    sys.exit(2)

inp, out = sys.stdin.buffer, sys.stdout.buffer
header = b''
while not header.endswith(b'\r\n\r\n'):
    b = inp.read(1)
    if not b:
        sys.exit(0)
    header += b
key = next(line.split(b':', 1)[1].strip() for line in header.split(b'\r\n') if line.lower().startswith(b'sec-websocket-key:'))
accept = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
out.write(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
out.flush()

def send(obj):
    payload = json.dumps(obj).encode()
    size = len(payload)
    prefix = bytes([0x81, size]) if size < 126 else b'\x81\x7e' + struct.pack('!H', size)
    out.write(prefix + payload)
    out.flush()

def event(method, params):
    send({'method': method, 'params': params})

project = {'id': 'p1', 'name': 'Fixture', 'roots': [{'path': os.environ.get('KODEX_FIXTURE_DIR', '/tmp')}]}
thread = {'id': 't1', 'name': 'Fixture task', 'preview': '', 'status': {'type': 'idle'}, 'updatedAt': int(time.time()) - 3600}
deferred = []
unpersisted = set()
archived = False
while True:
    header = inp.read(2)
    if len(header) != 2:
        break
    size = header[1] & 127
    if size == 126:
        size = struct.unpack('!H', inp.read(2))[0]
    elif size == 127:
        size = struct.unpack('!Q', inp.read(8))[0]
    mask = inp.read(4) if header[1] & 128 else b'\0' * 4
    payload = inp.read(size)
    if len(payload) != size:
        break
    msg = json.loads(bytes(value ^ mask[i % 4] for i, value in enumerate(payload)))
    if validate:
        try:
            validate(msg)
        except Exception as error:
            # This process handles only synthetic fixtures, never account data.
            (Path(__file__).parent.parent / 'build/protocol-validation-error.txt').write_text(str(error))
            raise
    if os.environ.get('KODEX_RECORD_REQUESTS'):
        with open(os.environ['KODEX_RECORD_REQUESTS'], 'a') as f:
            f.write(json.dumps(msg) + '\n')
    method = msg.get('method')
    if 'id' not in msg or not method:
        continue
    if method == 'thread/resume' and (msg['params']['threadId'] in unpersisted or archived):
        send({'id': msg['id'], 'error': {'code': -32600, 'message': 'no rollout found for thread id'}})
        continue
    if method == 'fixture/disconnect':
        sys.exit(0)
    if method == 'fixture/reorder':
        deferred.append(msg['id'])
        if len(deferred) == 2:
            for rid in reversed(deferred):
                send({'id': rid, 'result': {'ok': True}})
            deferred.clear()
        continue
    result = {}
    if method == 'initialize':
        result = {'userAgent': 'fixture'}
    elif method == 'account/read':
        result = {'account': {'type': 'chatgpt', 'email': 'fixture@example.test'}}
    elif method == 'model/list':
        result = {'data': [{'id': 'fixture-model', 'model': 'fixture-model', 'displayName': 'Fixture model', 'isDefault': True, 'defaultReasoningEffort': 'medium', 'supportedReasoningEfforts': [{'reasoningEffort': 'medium'}, {'reasoningEffort': 'high'}], 'serviceTiers': [{'id': 'fast', 'name': 'Fast'}]}]}
    elif method == 'project/list':
        result = {'data': [project]}
    elif method == 'project/create':
        result = {'project': project}
    elif method == 'thread/list':
        result = {'data': [thread] if bool(msg['params'].get('archived')) == archived else []}
    elif method == 'thread/archive':
        archived = True
    elif method == 'thread/unarchive':
        archived = False
    elif method == 'thread/read':
        result = {'thread': thread}
    elif method == 'thread/start':
        unpersisted.add('fresh-thread')
        result = {'thread': dict(thread, id='fresh-thread'), 'model': 'fixture-model', 'reasoningEffort': 'medium', 'cwd': project['roots'][0]['path']}
    elif method == 'thread/resume':
        result = {'thread': thread, 'model': 'fixture-model', 'reasoningEffort': 'medium'}
    elif method == 'thread/turns/list':
        result = {'data': [{'id': 'old-turn', 'status': 'completed', 'items': [{'id': 'old-user', 'type': 'userMessage', 'content': [{'type': 'text', 'text': 'Hello'}]}, {'id': 'old-agent', 'type': 'agentMessage', 'text': 'Earlier response'}]}]}
    elif method == 'mcpServerStatus/list':
        result = {'data': []}
    elif method == 'turn/start':
        result = {'turn': {'id': 'turn1', 'status': 'inProgress'}}
    elif method == 'remoteControl/status/read':
        result = {'status': 'connected', 'serverName': 'Fixture host', 'installationId': 'fixture'}
    elif method == 'remoteControl/pairing/start':
        result = {'environmentId': 'fixture', 'manualPairingCode': '123-456', 'pairingCode': 'fixture-code', 'expiresAt': int(time.time()) + 30}
    elif method == 'remoteControl/pairing/status':
        result = {'claimed': True}
    send({'id': msg['id'], 'result': result})
    if method == 'turn/start':
        event('turn/started', {'threadId': 't1', 'turn': result['turn']})
        event('item/started', {'threadId': 't1', 'item': {'id': 'user1', 'type': 'userMessage', 'content': msg['params']['input']}})
        event('item/started', {'threadId': 't1', 'item': {'id': 'agent1', 'type': 'agentMessage', 'text': ''}})
        event('item/agentMessage/delta', {'threadId': 't1', 'itemId': 'agent1', 'delta': 'Streamed '})
        event('item/agentMessage/delta', {'threadId': 't1', 'itemId': 'agent1', 'delta': 'response'})
        event('item/completed', {'threadId': 't1', 'item': {'id': 'agent1', 'type': 'agentMessage', 'text': 'Streamed response'}})
        event('thread/tokenUsage/updated', {'threadId': 't1', 'turnId': 'turn1', 'tokenUsage': {'last': {'totalTokens': 100}, 'total': {'totalTokens': 500}, 'modelContextWindow': 1000}})
        send({'id': 987, 'method': 'item/tool/requestUserInput', 'params': {'threadId': 't1', 'turnId': 'turn1', 'itemId': 'question1', 'questions': [{'id': 'q1', 'question': 'Choose?', 'options': [{'label': 'Yes'}]}]}})
        event('turn/completed', {'threadId': 't1', 'turn': {'id': 'turn1', 'status': 'completed'}})
