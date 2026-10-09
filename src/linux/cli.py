"""Linux counterpart of the existing lcb-cli PORT PROMPT command."""
import sys
from backend import Core, Transport


def main():
    if len(sys.argv) != 3:
        print('usage: lcb-cli PORT PROMPT', file=sys.stderr)
        return 2
    try:
        core = Core()
        transport = Transport(int(sys.argv[1]))
        wire = core.request([], 'You are a helpful local assistant. Answer clearly and concisely.',
                            sys.argv[2], core.defaults())
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 2
    try:
        reply = transport.generate(core, wire, lambda text: None)
        if reply['status'] not in ('complete', 'length', 'other'):
            print(reply['error'], file=sys.stderr)
            return 1
        print(reply['content'])
        return 0
    except Exception as exc:
        print(exc, file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
