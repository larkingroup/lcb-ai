"""Exercise the compiled transport against controlled loopback responses."""
import http.server
import json
from pathlib import Path
import subprocess
import threading
import unittest

EXE = Path(__file__).resolve().parents[1] / "build" / "lti-cli.exe"


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        self.server.requests.append((self.path, body))
        mode = body["messages"][-1]["content"]
        status, reply = 200, json.dumps({"choices": [{"message": {"content": "Local reply \u263a"}}]}).encode()
        if mode == "redirect":
            self.send_response(302)
            self.send_header("Location", f"http://127.0.0.1:{self.server.server_port}/forbidden")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if mode == "failure":
            status = 503
        if mode == "malformed":
            reply = b'{"choices":'
        if mode == "oversized":
            reply = b"x" * (1048576 + 1)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)


class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.server.requests = []
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def ask(self, prompt):
        return subprocess.run([str(EXE), str(self.server.server_port), prompt],
                              capture_output=True, encoding="utf-8", timeout=15)

    def test_success_and_request_contract(self):
        r = self.ask('a "quoted" prompt\nwith newline')
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.strip(), "Local reply \u263a")
        path, body = self.server.requests[-1]
        self.assertEqual(path, "/v1/chat/completions")
        self.assertFalse(body["stream"])
        self.assertEqual(body["messages"][-1]["content"], 'a "quoted" prompt\nwith newline')

    def test_reject_failures(self):
        for prompt, error in [("failure", "503"), ("malformed", "invalid"),
                              ("oversized", "1 MiB"), ("redirect", "302")]:
            with self.subTest(prompt=prompt):
                r = self.ask(prompt)
                self.assertEqual(r.returncode, 1)
                self.assertIn(error, r.stderr)
        self.assertTrue(all(path != "/forbidden" for path, _ in self.server.requests))

    def test_invalid_ports(self):
        for port in ["0", "65536", "-1", "8080x", "https://example.com"]:
            r = subprocess.run([str(EXE), port, "hello"], capture_output=True, timeout=5)
            self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
