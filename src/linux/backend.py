"""Native Linux services around the original LCB C request/model/stream core."""
import configparser
import base64
import copy
import ctypes as C
import fcntl
import hashlib
import http.client
import json
import locale
import os
from pathlib import Path
import re
import signal
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import uuid

MAX_PROMPT, MAX_REPLY, MAX_WIRE = 16384, 65536, 1048576
MAX_MEDIA = 16 * MAX_WIRE
MAX_MEDIA_WIRE = 64 * MAX_WIRE
MEDIA_TYPES = {'.png': ('vision', 'image/png'), '.jpg': ('vision', 'image/jpeg'),
               '.jpeg': ('vision', 'image/jpeg'),
               '.bmp': ('vision', 'image/bmp'), '.gif': ('vision', 'image/gif'),
               '.wav': ('audio', 'audio/wav'), '.mp3': ('audio', 'audio/mpeg'),
               '.flac': ('audio', 'audio/flac'), '.mp4': ('video', 'video/mp4'),
               '.mkv': ('video', 'video/x-matroska'), '.webm': ('video', 'video/webm'),
               '.mov': ('video', 'video/quicktime')}


def validate_attachments(items):
    if not isinstance(items, list) or len(items) > 8:
        raise ValueError('Use at most 8 attachments per message.')
    for item in items:
        if not isinstance(item, dict) or not re.fullmatch(r'[a-f0-9]{64}\.[a-z0-9]+', item.get('file', '')):
            raise ValueError('Invalid saved attachment.')
        suffix = Path(item['file']).suffix
        if suffix not in MEDIA_TYPES or item.get('kind') != MEDIA_TYPES[suffix][0]:
            raise ValueError('Invalid attachment format.')
        text_ok(item.get('name'), 1024, False)
        if type(item.get('size')) is not int or not 0 < item['size'] <= MAX_MEDIA:
            raise ValueError('Each attachment must be between 1 byte and 16 MiB.')


def model_role(model):
    if model.projector:
        return 'Projector companion'
    if re.search(r'(?:FastMTP|(?:^|[-_])MTP[-_](?:Q\d|F16|BF16|32K))', Path(model.path).name, re.I):
        return 'Draft companion'
    if model.filetype == 143:
        return 'Requires PTQ engine'
    return 'Chat model'


def suggested_projector(core, model):
    """Suggest only an unambiguous same-release filename match, never cross-pair models."""
    def stem(path):
        value = Path(path).stem.casefold().removeprefix('mmproj-')
        return re.sub(r'-(?:iq\d[^-]*|q\d[^-]*|bf16|f16|f32)$', '', value)
    matches = [p for p in Path(model.path).parent.glob('*.gguf')
               if p.name.lower().startswith('mmproj-') and stem(p) == stem(model.path)]
    try:
        return str(matches[0]) if len(matches) == 1 and core.model(matches[0]).projector else ''
    except (ValueError, OSError):
        return ''
FIELDS = ('max_tokens', 'temperature', 'top_p', 'context_tokens', 'thinking',
          'repeat_penalty', 'dry_multiplier', 'top_k', 'min_p', 'presence_penalty')
LABELS = ('Response tokens', 'Temperature', 'Top P', 'Context (reload)', 'Thinking',
          'Repeat penalty', 'DRY multiplier', 'Top K', 'Min P', 'Presence penalty')
STATUSES = ('unknown', 'complete', 'stopped', 'length', 'error', 'other')

class Generation(C.Structure):
    _fields_ = [('max_tokens', C.c_int), ('temperature', C.c_double), ('top_p', C.c_double),
                ('context_tokens', C.c_int), ('thinking', C.c_int), ('repeat_penalty', C.c_double),
                ('dry_multiplier', C.c_double), ('top_k', C.c_int), ('min_p', C.c_double),
                ('presence_penalty', C.c_double)]

class Message(C.Structure):
    _fields_ = [('role', C.c_char_p), ('text', C.c_char_p), ('status', C.c_int), ('error', C.c_char * 256)]

class Conversation(C.Structure):
    _fields_ = [('messages', C.POINTER(Message)), ('count', C.c_size_t),
                ('capacity', C.c_size_t), ('bytes', C.c_size_t)]

class Module(C.Structure):
    _fields_ = [('name', C.c_char_p), ('instruction', C.c_char_p)]

class Stream(C.Structure):
    _fields_ = [('line', C.c_char * (MAX_REPLY + 1024)), ('text', C.c_char * (MAX_REPLY + 1)),
                ('line_used', C.c_size_t), ('used', C.c_size_t), ('wire', C.c_size_t),
                ('done', C.c_int), ('failed', C.c_int), ('tokens', C.c_int),
                ('prompt_tokens', C.c_int), ('finish', C.c_int), ('saw_reasoning', C.c_int),
                ('reasoning', C.c_char * (MAX_REPLY + 1)), ('reasoning_used', C.c_size_t),
                ('progress_total', C.c_int), ('progress_processed', C.c_int),
                ('progress_cached', C.c_int), ('progress_ms', C.c_double)]

class Model(C.Structure):
    _fields_ = [('path', C.c_wchar * 4096), ('name', C.c_wchar * 256),
                ('architecture', C.c_wchar * 80), ('size', C.c_wchar * 40),
                ('identity', C.c_wchar * 256), ('basename', C.c_wchar * 256),
                ('base_model', C.c_wchar * 256), ('sampling', Generation),
                ('sampling_fields', C.c_uint), ('thinking_switch', C.c_int),
                ('base_count', C.c_uint32), ('bytes', C.c_uint64),
                ('filetype', C.c_uint32), ('projector', C.c_int)]

class TooLarge(ValueError):
    pass


class Core:
    def __init__(self):
        locale.setlocale(locale.LC_CTYPE, '')
        self.lib = C.CDLL(os.environ['LCB_NATIVE_LIBRARY'])
        lib = self.lib
        lib.generation_defaults.restype = Generation
        lib.generation_set.argtypes = [C.POINTER(Generation), C.c_int, C.c_double]
        lib.generation_valid.argtypes = [C.POINTER(Generation)]
        lib.conversation_add.argtypes = [C.POINTER(Conversation), C.c_char_p, C.c_char_p]
        lib.conversation_clear.argtypes = [C.POINTER(Conversation)]
        lib.conversation_generate.argtypes = [C.POINTER(Conversation), C.POINTER(Module), C.c_char_p, C.POINTER(Generation)]
        lib.conversation_generate.restype = C.c_void_p
        lib.cJSON_free.argtypes = [C.c_void_p]
        lib.stream_feed.argtypes = [C.POINTER(Stream), C.c_char_p, C.c_size_t]
        lib.model_read.argtypes = [C.c_wchar_p, C.POINTER(Model)]
        lib.model_defaults.argtypes = [C.POINTER(Model), C.c_int, C.POINTER(Generation)]
        lib.model_quant.argtypes = [C.c_uint32]
        lib.model_quant.restype = C.c_wchar_p

    def model(self, path):
        m = Model()
        if not self.lib.model_read(str(Path(path).resolve()), C.byref(m)):
            raise ValueError('Cannot read this GGUF model header.')
        return m

    def defaults(self, model=None, thinking=-1):
        g = self.lib.generation_defaults()
        self.lib.model_defaults(C.byref(model) if model else None, thinking, C.byref(g))
        return g

    def set(self, g, field, value):
        if not self.lib.generation_set(C.byref(g), field, float(value)):
            raise ValueError(f'Invalid {LABELS[field]} value.')

    def request(self, messages, instruction, prompt, g):
        text_ok(instruction, MAX_PROMPT)
        text_ok(prompt, MAX_PROMPT, False)
        c = Conversation()
        try:
            for i, m in enumerate(messages):
                role = b'assistant' if i % 2 else b'user'
                if not self.lib.conversation_add(C.byref(c), role, m['content'].encode()):
                    raise ValueError('Cannot prepare conversation.')
            module = Module(b'Workspace', instruction.encode())
            result = self.lib.conversation_generate(C.byref(c), C.byref(module), prompt.encode(), C.byref(g))
            if not result:
                raise ValueError('Conversation request is invalid or too large.')
            try:
                wire = C.string_at(result)
                if len(wire) > MAX_WIRE:
                    raise TooLarge('Request exceeds 1 MiB.')
                return wire
            finally:
                self.lib.cJSON_free(result)
        finally:
            self.lib.conversation_clear(C.byref(c))


def text_ok(value, limit, empty=True):
    if not isinstance(value, str) or '\0' in value or len(value.encode()) > limit or (not empty and not value):
        raise ValueError(f'Text must contain {"0" if empty else "1"}–{limit} UTF-8 bytes, without NUL characters.')


def atomic_write(path, data):
    path = Path(path)
    fd, temp = tempfile.mkstemp(prefix='.save-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temp, path)
        fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def data_root():
    if os.environ.get('LCB_DATA_HOME'):
        return Path(os.environ['LCB_DATA_HOME']).expanduser().resolve()
    base = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    for name in ('lcb-ai', 'lts-ai', 'lti-ai'):
        if (base / name).exists():
            return base / name
    return base / 'lcb-ai'


class Store:
    """Version-1 JSON files remain readable by the Windows application."""
    def __init__(self, root=None):
        self.root = Path(root) if root else data_root()
        self.root.mkdir(parents=True, exist_ok=True)
        self.lock = (self.root / 'session.lock').open('a+b')
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.lock.close()
            raise RuntimeError('Storage is in use. Close the other LCB-AI window.')
        self.workspaces, self.chats, self.skipped = {}, {}, []
        for folder in ('workspaces', 'chats'):
            (self.root / folder).mkdir(exist_ok=True)
            for path in sorted((self.root / folder).glob('*.json')):
                try:
                    if path.is_symlink() or path.stat().st_size >= 2**31:
                        raise ValueError('Invalid file')
                    obj = json.loads(path.read_text('utf-8'))
                    self.validate(folder, obj)
                    if obj['id'] != path.stem:
                        raise ValueError('Identifier mismatch')
                    getattr(self, folder)[obj['id']] = obj
                except (OSError, ValueError, KeyError, TypeError):
                    self.skipped.append(str(path))
        if not self.workspaces:
            self.workspace('General', 'You are a helpful local assistant. Answer clearly and concisely.')

    def validate(self, folder, obj):
        if not isinstance(obj, dict) or obj.get('version') != 1 or not re.fullmatch('[a-f0-9]{32}', obj.get('id', '')):
            raise ValueError('Invalid saved identifier/version')
        if folder == 'workspaces':
            text_ok(obj['name'], 240, False)
            text_ok(obj['master_prompt'], MAX_PROMPT)
        else:
            if obj['workspace'] not in self.workspaces:
                raise ValueError('Workspace not found')
            text_ok(obj['title'], 240, False)
            text_ok(obj['draft'], MAX_PROMPT)
            validate_attachments(obj.get('draft_attachments', []))
            if not isinstance(obj['messages'], list) or len(obj['messages']) % 2:
                raise ValueError('Incomplete saved conversation')
            for i, m in enumerate(obj['messages']):
                if m['role'] != ('assistant' if i % 2 else 'user'):
                    raise ValueError('Invalid message order')
                text_ok(m['content'], MAX_REPLY, False)
                validate_attachments(m.get('attachments', []))
                if i % 2:
                    if m.get('status', 'unknown') not in STATUSES:
                        raise ValueError('Invalid answer status')
                    text_ok(m.get('error', ''), 255)
                    text_ok(m.get('reasoning_content', ''), MAX_REPLY)
                    text_ok(m.get('model', ''), 4096)

    def save(self, folder, obj):
        self.validate(folder, obj)
        index = getattr(self, folder)
        if obj['id'] not in index and len(index) >= (64 if folder == 'workspaces' else 1024):
            raise ValueError('Saved workspace/conversation limit reached.')
        atomic_write(self.root / folder / (obj['id'] + '.json'), json.dumps(obj, ensure_ascii=False).encode())
        index[obj['id']] = copy.deepcopy(obj)

    def workspace(self, name, prompt, identifier=None):
        obj = dict(version=1, id=identifier or uuid.uuid4().hex, name=name, master_prompt=prompt)
        self.save('workspaces', obj)
        return obj

    def new(self, workspace):
        obj = dict(version=1, id=uuid.uuid4().hex, workspace=workspace, title='New chat', draft='', messages=[])
        self.save('chats', obj)
        return obj

    def branch(self, chat, turn, prompt, action):
        if not 0 <= turn < len(chat['messages']) // 2:
            raise ValueError('Choose an exchange first.')
        text_ok(prompt, MAX_PROMPT, False)
        obj = copy.deepcopy(chat)
        obj.update(id=uuid.uuid4().hex, title=truncate(f'{action} {turn + 1}: {chat["title"]}', 240),
                   draft=prompt, draft_attachments=copy.deepcopy(chat['messages'][turn * 2].get('attachments', [])),
                   messages=obj['messages'][:turn * 2])
        self.save('chats', obj)
        return obj

    def delete(self, identifier):
        if identifier not in self.chats:
            raise ValueError('Conversation not found.')
        (self.root / 'chats' / (identifier + '.json')).unlink()
        del self.chats[identifier]

    def close(self):
        self.lock.close()

    def import_attachment(self, source):
        source = Path(source)
        suffix = source.suffix.lower()
        if suffix not in MEDIA_TYPES or not source.is_file():
            raise ValueError('Choose a supported image, audio, or video file.')
        with source.open('rb') as stream:
            data = stream.read(MAX_MEDIA + 1)
        if not 0 < len(data) <= MAX_MEDIA:
            raise ValueError('Each attachment must be between 1 byte and 16 MiB.')
        item = dict(file=hashlib.sha256(data).hexdigest() + suffix, name=source.name,
                    kind=MEDIA_TYPES[suffix][0], size=len(data))
        validate_attachments([item])
        folder = self.root / 'attachments'
        folder.mkdir(mode=0o700, exist_ok=True)
        target = folder / item['file']
        if not target.exists():
            atomic_write(target, data)
        return item

    def media_part(self, item):
        validate_attachments([item])
        path = self.root / 'attachments' / item['file']
        if path.is_symlink():
            raise ValueError('Attachment storage must contain regular files.')
        with path.open('rb') as stream:
            data = stream.read(MAX_MEDIA + 1)
        if len(data) != item['size'] or hashlib.sha256(data).hexdigest() != Path(item['file']).stem:
            raise ValueError('Saved attachment changed or is incomplete: ' + item['name'])
        encoded = base64.b64encode(data).decode('ascii')
        if item['kind'] == 'vision':
            return dict(type='image_url', image_url={'url': 'data:' + MEDIA_TYPES[path.suffix][1] + ';base64,' + encoded})
        key = 'input_' + item['kind']
        return {'type': key, key: {'data': encoded, 'format': path.suffix[1:]}}


def truncate(text, length):
    return text.encode()[:length].decode('utf-8', 'ignore')


class Settings:
    def __init__(self, root, core):
        self.path, self.core = Path(root) / 'settings.ini', core
        self.ini = configparser.ConfigParser(interpolation=None)
        if self.path.exists():
            raw = self.path.read_bytes()
            self.ini.read_string(raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))

    def get(self, section, key, default=''):
        return self.ini.get(section, key, fallback=default)

    def save(self, section, key, value):
        old = copy.deepcopy(self.ini)
        if not self.ini.has_section(section):
            self.ini.add_section(section)
        self.ini.set(section, key, str(value))
        import io
        out = io.StringIO()
        self.ini.write(out)
        try:
            atomic_write(self.path, out.getvalue().encode())
        except Exception:
            self.ini = old
            raise

    def section(self, model):
        # Linux paths are case-sensitive; do not reuse Windows' case-folded hash.
        return 'linux-model-' + hashlib.sha256(os.fsencode(os.path.abspath(model))).hexdigest()

    def import_windows_model(self, old_path, new_path):
        """Carry choices when the user relocates the previously selected Windows model."""
        if not re.match(r'^[A-Za-z]:[\\/]', old_path):
            return
        if old_path.replace('\\', '/').rsplit('/', 1)[-1] != Path(new_path).name:
            return
        legacy = 'model-' + hashlib.sha256(old_path.replace('/', '\\').lower().encode('utf-16le')).hexdigest()
        target = self.section(new_path)
        for key in FIELDS:
            value = self.get(legacy, key)
            if value and not self.get(target, key):
                self.save(target, key, value)

    def generation(self, path, model=None):
        section = self.section(path) if path else 'no-model'
        thinking = self.get(section, 'thinking', '-1')
        g = self.core.defaults(model, int(thinking) if thinking in ('0', '1', '2') else -1)
        g.thinking = 0  # Unset means use the model's embedded template default.
        for key, field, scale in (('max_tokens', 0, 1), ('temperature100', 1, 100), ('top_p100', 2, 100)):
            value = self.get('generation', key)
            if value:
                try:
                    self.core.set(g, field, float(value) / scale)
                except ValueError:
                    pass
        for i, key in enumerate(FIELDS):
            value = self.get(section, key)
            if value:
                try:
                    self.core.set(g, i, value)
                except ValueError:
                    pass
        return g


class Cancelled(Exception):
    pass

class Transport:
    def __init__(self, port):
        if not 1 <= int(port) <= 65535:
            raise ValueError('Port must be 1–65535.')
        self.port = int(port)
        self.cancelled = threading.Event()
        self.socket = None

    def cancel(self):
        self.cancelled.set()
        if self.socket:
            try:
                self.socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def check(self):
        if self.cancelled.is_set():
            raise Cancelled('Generation stopped.')

    def exchange(self, path, body=None, receive=None, timeout=90, deadline=120):
        self.check()
        if body is not None and not isinstance(body, bytes):
            body = json.dumps(body).encode()
        limit = MAX_MEDIA_WIRE if path == '/v1/chat/completions' else MAX_WIRE
        if body and len(body) > limit:
            raise TooLarge(f'Request exceeds {limit // MAX_WIRE} MiB. Use fewer or smaller attachments.')
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=timeout)
        start = time.monotonic()
        watchdog = threading.Timer(deadline, self._deadline) if deadline else None
        self.timed_out = False
        if watchdog:
            watchdog.start()
        try:
            connection.connect()
            self.socket = connection.sock
            if self.timed_out:
                raise TimeoutError('Local request deadline exceeded.')
            self.check()
            connection.request('POST' if body is not None else 'GET', path, body,
                               {'Content-Type': 'application/json', 'Connection': 'close'})
            response = connection.getresponse()
            if response.status != 200:
                detail = ''
                try:
                    raw = response.read(8192)
                    payload = json.loads(raw)
                    detail = payload.get('error', {}).get('message', '')
                except (ValueError, AttributeError):
                    pass
                raise EngineHTTPError(response.status, detail)
            parts, size = [], 0
            while True:
                self.check()
                chunk = response.read1(4096)
                if not chunk:
                    break
                size += len(chunk)
                limit = 16 * MAX_WIRE if receive else MAX_WIRE
                if size > limit:
                    raise TooLarge(f'Local response exceeds {limit // MAX_WIRE} MiB.')
                if self.timed_out or (deadline and time.monotonic() - start > deadline):
                    raise ValueError('Local response deadline exceeded.')
                if receive:
                    if receive(chunk):
                        break
                else:
                    parts.append(chunk)
            self.check()
            return b''.join(parts)
        finally:
            if watchdog:
                watchdog.cancel()
            self.socket = None
            connection.close()

    def _deadline(self):
        self.timed_out = True
        if self.socket:
            try:
                self.socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def json(self, path, body=None):
        obj = json.loads(self.exchange(path, body))
        if not isinstance(obj, dict):
            raise ValueError('Invalid JSON from the local engine.')
        return obj

    def prepare(self, core, model, messages, instruction, prompt, g, attachments=None, store=None):
        props = self.json('/props')
        context = props.get('default_generation_settings', {}).get('n_ctx')
        template = props.get('chat_template')
        if type(context) is not int or context <= 0 or not isinstance(template, str) or not template:
            raise ValueError('Engine must report its context size and chat template.')
        if model and os.path.realpath(props.get('model_path', '')) != os.path.realpath(model):
            raise ValueError('The local server is running a different model. Select its GGUF file.')
        thinking = 'enable_thinking' in template
        if g.thinking and not thinking:
            raise ValueError('This template has no thinking switch. Set Thinking to Auto.')
        available = context - g.max_tokens - 32
        if available < 1:
            raise ValueError('Response limit leaves no prompt room. Lower it or reload with more context.')

        attachment_sets = [m.get('attachments', []) for m in messages] + [attachments or []]
        if any(attachment_sets):
            # Text /tokenize cannot count image/audio embeddings. Let libmtmd
            # validate the full prompt; never silently discard media or history.
            modalities = props.get('modalities', {})
            for items in attachment_sets:
                validate_attachments(items)
                for item in items:
                    if modalities.get(item['kind']) is not True:
                        raise ValueError(f'The loaded engine does not support {item["kind"]} input. Load a matching projector/model, or remove the attachment ({item["name"]}).')
                    if item['kind'] == 'video' and not all(shutil.which(exe) for exe in ('ffmpeg', 'ffprobe')):
                        raise ValueError('Video requires ffmpeg and ffprobe on PATH. On Fedora: sudo dnf install ffmpeg-free. Reload the engine after installing them.')
            if store is None:
                raise ValueError('Attachment storage is unavailable.')
            if sum(a['size'] for items in attachment_sets for a in items) > 46 * MAX_WIRE:
                raise TooLarge('Conversation media exceeds 46 MiB. Start a new chat or use smaller files.')
            request = json.loads(core.request(messages, instruction, prompt, g))
            turns = [m for m in request['messages'] if m['role'] != 'system']
            for turn, items in zip(turns, attachment_sets):
                if items:
                    self.check()
                    turn['content'] = [{'type': 'text', 'text': turn['content']}] + [store.media_part(a) for a in items]
            request.update(reasoning_format='deepseek', return_progress=True, sse_ping_interval=5)
            wire = json.dumps(request, ensure_ascii=False).encode()
            if len(wire) > MAX_MEDIA_WIRE:
                raise TooLarge('Media request exceeds 64 MiB. Use smaller attachments.')
            return wire, dict(context=context, prompt=None, response=g.max_tokens, omitted=0, thinking=thinking)

        def counted(first):
            wire = core.request(messages[first:], instruction, prompt, g)
            formatted = self.json('/apply-template', wire).get('prompt')
            if not isinstance(formatted, str):
                raise ValueError('Engine could not apply the chat template.')
            tokens = self.json('/tokenize', {'content': formatted, 'add_special': True, 'parse_special': True}).get('tokens')
            if not isinstance(tokens, list) or any(type(t) is not int or t < 0 for t in tokens):
                raise ValueError('Invalid token count from the engine.')
            return wire, len(tokens)

        first = len(messages)
        wire, count = counted(first)
        if count > available:
            raise ValueError(f'Instructions and message need {count} tokens; only {available} available.')
        low, high = 0, len(messages) // 2
        while low < high:
            self.check()
            mid = (low + high + 1) // 2
            suffix = messages[len(messages) - mid * 2:]
            if sum(len(m['content'].encode()) for m in suffix) * 6 + len(suffix) * 64 + 200000 > MAX_WIRE:
                high = mid - 1
                continue
            try:
                candidate, n = counted(len(messages) - mid * 2)
            except TooLarge:
                high = mid - 1
                continue
            if n <= available:
                low, wire, count, first = mid, candidate, n, len(messages) - mid * 2
            else:
                high = mid - 1
        request = json.loads(wire)
        request.update(reasoning_format='deepseek', return_progress=True, sse_ping_interval=5)
        wire = json.dumps(request, ensure_ascii=False).encode()
        return wire, dict(context=context, prompt=count, response=g.max_tokens, omitted=first // 2, thinking=thinking)

    def generate(self, core, wire, update, telemetry=None):
        state = Stream()
        error, result = '', 1
        last = 0
        def receive(chunk):
            nonlocal last
            if not core.lib.stream_feed(C.byref(state), chunk, len(chunk)):
                raise ValueError('Invalid or oversized stream from the local engine.')
            now = time.monotonic()
            if now - last > .05 or state.done:
                if state.used:
                    update(bytes(state.text).decode('utf-8'))
                if telemetry:
                    telemetry(dict(content=bytes(state.text).decode('utf-8'),
                        reasoning_content=bytes(state.reasoning).decode('utf-8'),
                        total=state.progress_total, processed=state.progress_processed,
                        cached=state.progress_cached, time_ms=state.progress_ms))
                last = now if state.used or state.reasoning_used or state.progress_total else 0
            return bool(state.done)
        try:
            self.exchange('/v1/chat/completions', wire, receive, timeout=90, deadline=None)
            if not state.done or state.failed:
                raise ValueError('Engine closed an incomplete response stream.')
        except Exception as exc:
            result = 2 if self.cancelled.is_set() else 0
            error = 'Stopped by user.' if result == 2 else str(exc)
        answer = bytes(state.text).decode('utf-8', 'replace')
        if result == 1 and not answer and not state.reasoning_used:
            result, error = 0, 'The model ended without emitting an answer or a thinking trace.'
        status = 'stopped' if result == 2 else 'error' if not result else {1: 'complete', 2: 'length'}.get(state.finish, 'other')
        return dict(content=answer, status=status, error=truncate(error, 255), tokens=state.tokens,
                    prompt_tokens=state.prompt_tokens, reasoning=bool(state.saw_reasoning),
                    reasoning_content=bytes(state.reasoning).decode('utf-8', 'replace'))


class EngineHTTPError(ValueError):
    def __init__(self, status, detail=''):
        self.status = status
        super().__init__(f'Local engine returned HTTP {status}: {detail or "check that the model is ready"}')


class Engine:
    def __init__(self):
        self.process = None
        self.log_path = None
        self.log_offset = 0
        self.log_tail = ''
        self.command = []

    def read_log(self):
        if not self.log_path:
            return ''
        try:
            with open(self.log_path, 'rb') as log:
                log.seek(self.log_offset)
                data = log.read(65536)
                self.log_offset = log.tell()
            text = data.decode('utf-8', 'replace')
            self.log_tail = (self.log_tail + text)[-16000:]
            return text
        except OSError:
            return ''

    def failure(self):
        if 'invalid ggml type' in self.log_tail:
            return 'This model uses a tensor format that the selected llama.cpp engine does not support. Choose a compatible GGUF or its required engine build.'
        errors = [line for line in self.log_tail.splitlines() if re.search(r'error|failed|unsupported|out of memory', line, re.I)]
        return '\n'.join(errors[-4:]) or 'The engine exited before loading. See the live engine log.'

    def start(self, exe, model, port, context, log, projector=''):
        if self.process:
            raise ValueError('Unload the current model first.')
        exe, model = str(Path(exe).resolve()), str(Path(model).resolve())
        if not Path(exe).is_file() or not os.access(exe, os.X_OK):
            raise ValueError('Choose an executable Linux llama-server first.')
        with open(exe, 'rb') as binary:
            if binary.read(2) == b'MZ':
                raise ValueError('Select a Linux llama-server binary; Windows .exe files cannot run natively.')
        if not Path(model).is_file():
            raise ValueError('Choose an available GGUF model.')
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', port))
        args = [exe, '--model', model, '--alias', 'local', '--host', '127.0.0.1', '--port', str(port),
                '--ctx-size', str(context), '--parallel', '1', '--jinja', '--reasoning-format', 'deepseek',
                '--sse-ping-interval', '5', '--no-webui',
                '--no-agent', '--no-ui-mcp-proxy', '--cors-origins', 'localhost', '--no-cors-credentials']
        if projector:
            if not Path(projector).is_file():
                raise ValueError('The selected projector is unavailable.')
            args += ['--mmproj', str(Path(projector).resolve())]
        self.command = args
        self.log_path = Path(log)
        self.log_tail = ''
        self.log_offset = self.log_path.stat().st_size if self.log_path.exists() else 0
        with open(log, 'ab', buffering=0) as output:
            self.process = subprocess.Popen(args, cwd=str(Path(exe).parent), stdout=output,
                                            stderr=subprocess.STDOUT, start_new_session=True)

    def stop(self):
        p = self.process
        if not p:
            return
        try:
            os.killpg(p.pid, signal.SIGTERM)
            try:
                p.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait(timeout=2)
        except ProcessLookupError:
            p.wait()
        self.process = None


def scan_models(core, folder, recursive, cancel):
    if not Path(folder).is_dir():
        raise ValueError('Choose an existing model folder.')
    models, skipped, folders = [], 0, 0
    for base, dirs, files in os.walk(folder, followlinks=False):
        folders += 1
        if cancel.is_set() or folders > 4096:
            break
        dirs[:] = sorted(d for d in dirs if not Path(base, d).is_symlink()) if recursive else []
        if len(Path(base).relative_to(folder).parts) >= 24:
            dirs.clear()
        for file in sorted(files):
            if cancel.is_set() or len(models) >= 512:
                return models, skipped
            path = Path(base, file)
            if path.suffix.lower() != '.gguf' or path.is_symlink():
                continue
            try:
                model = core.model(path)
                model.projector_hint = suggested_projector(core, model) if model_role(model) == 'Chat model' else ''
                models.append(model)
            except ValueError:
                skipped += 1
    return models, skipped
