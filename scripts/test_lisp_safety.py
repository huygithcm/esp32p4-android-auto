"""Host branch tests of actual main.lisp forms, NOT a LispBM runtime.

Strict, intentionally limited evaluator: no scheduling, firmware APIs, VM
memory/const semantics, GPIO or numeric-width fidelity. Unsupported forms fail.
Run: python scripts/test_lisp_safety.py
"""
import operator
import re
from pathlib import Path


class Symbol(str):
    pass


def parse(source):
    tokens = [(m.group(), source.count('\n', 0, m.start()) + 1) for m in
              re.finditer(r';[^\n]*|"(?:\\.|[^"\\])*"|[(){}\x27]|[^\s(){}\x27;]+', source)
              if not m.group().startswith(';')]
    pos = 0

    def item():
        nonlocal pos
        token, line = tokens[pos]
        pos += 1
        if token in ('(', '{'):
            close = ')' if token == '(' else '}'
            out = [] if token == '(' else [Symbol('progn')]
            while pos < len(tokens) and tokens[pos][0] != close:
                if tokens[pos][0] in (')', '}'):
                    raise SyntaxError(f'line {tokens[pos][1]}: unexpected {tokens[pos][0]}, expected {close} for line {line}')
                out.append(item())
            if pos == len(tokens):
                raise SyntaxError(f'line {line}: unclosed {token}')
            pos += 1
            return out
        if token in (')', '}'):
            raise SyntaxError(f'line {line}: unexpected {token}')
        if token == "'":
            return [Symbol('quote'), item()]
        if token.startswith('"'):
            return token[1:-1]
        try:
            return int(token, 0)
        except ValueError:
            try:
                return float(token)
            except ValueError:
                return Symbol(token)

    forms = []
    while pos < len(tokens):
        forms.append(item())
    return forms


class VM:
    def __init__(self, forms):
        self.g = {'t': True, 'nil': None}
        self.funcs = {}
        self.now = 10.0
        self.speed = 0
        self.adc = [0, 0]
        self.output = []
        self.config = {'adc-ctrl-type': 0, 'l-current-max': 70,
                       'l-current-min': -30, 'adc-ramp-time-pos': .5,
                       'adc-ramp-time-neg': .5}
        self.native_ok = True
        self.ops = {'+': lambda *x: sum(x), '-': lambda a, b: a-b,
                    '*': operator.mul, '/': operator.truediv, 'mod': operator.mod,
                    '=': operator.eq, '<': operator.lt, '>': operator.gt,
                    '<=': operator.le, '>=': operator.ge, 'abs': abs,
                    'to-i': int, 'to-float': float, 'to-str': str,
                    'str-merge': lambda *x: ''.join(x), 'print': lambda *x: None,
                    'systime': lambda: self.now,
                    'secs-since': lambda t: self.now-t,
                    'get-speed': lambda: self.speed,
                    'get-adc-decoded': lambda i: self.adc[i],
                    'conf-get': lambda k: self.config[k],
                    'conf-set': lambda k, v: self.config.__setitem__(k, v),
                    'app-disable-output': lambda *x: None,
                    'app-adc-range-ok': lambda: self.native_ok,
                    'throttle-curve': lambda t, *x: t,
                    'bufcreate': bytearray, 'eeprom-read-i': lambda *x: None}
        self.sent = []
        self.ops['bufset-u8'] = lambda b, i, v: b.__setitem__(i, int(v) & 255)
        self.ops['send-data'] = lambda b, *args: self.sent.append(bytes(b))
        self.ops['bufcpy'] = lambda dst, di, src, si, n: dst.__setitem__(slice(di, di+n), src[si:si+n])
        for name in ['set-current', 'set-current-rel', 'set-brake-rel']:
            self.ops[name] = lambda *x, n=name: self.output.append((n, x[0]))
        for form in forms:
            if isinstance(form, list) and form and form[0] == 'defun':
                self.funcs[form[1]] = (form[2], form[3:])
            elif isinstance(form, list) and form and form[0] == 'def':
                self.g[form[1]] = self.ev(form[2], {})
        # Explicit firmware API stub, not evidence the native API exists.
        self.ops['native-input-only'] = lambda: self.config['adc-ctrl-type'] == 0

    def ev(self, x, local):
        if isinstance(x, Symbol):
            return local[x] if x in local else self.g[x]
        if not isinstance(x, list):
            return x
        if not x:
            return None
        op, *args = x
        if op == 'quote':
            return args[0]
        if op == 'progn':
            value = None
            for expr in args:
                value = self.ev(expr, local)
            return value
        if op == 'if':
            return self.ev(args[1], local) if self.ev(args[0], local) else (self.ev(args[2], local) if len(args) > 2 else None)
        if op in ('and', 'or'):
            return all(self.ev(a, local) for a in args) if op == 'and' else any(self.ev(a, local) for a in args)
        if op == 'not':
            return not self.ev(args[0], local)
        if op == 'setq':
            target = local if args[0] in local else self.g
            target[args[0]] = self.ev(args[1], local)
            return target[args[0]]
        if op == 'let':
            scope = dict(local)
            scope.update({name: self.ev(value, local) for name, value in args[0]})
            return self.ev([Symbol('progn'), *args[1:]], scope)
        if op == 'cond':
            for branch in args:
                if self.ev(branch[0], local):
                    return self.ev([Symbol('progn'), *branch[1:]], local)
            return None
        return self.call(op, *[self.ev(a, local) for a in args])

    def call(self, name, *args):
        if name in self.ops:
            return self.ops[name](*args)
        params, body = self.funcs[name]
        assert len(params) == len(args), name
        return self.ev([Symbol('progn'), *body], dict(zip(params, args)))


def run(forms):
    results = []

    def case(name, test):
        vm = VM(forms)
        vm.g.update({'motor-live': 1, 'motor-seen': vm.now})
        try:
            test(vm)
            results.append(True)
            print('PASS', name)
        except Exception as exc:
            results.append(False)
            print('FAIL', name, type(exc).__name__, str(exc))

    def eq(actual, expected):
        assert actual == expected, f'{actual!r} != {expected!r}'

    def park_guards(v):
        v.speed = -.1
        eq(v.call('ride-set-park', 0), 5)
        v.speed = 0
        v.adc[0] = .2
        eq(v.call('ride-set-park', 0), 6)
        v.adc[0] = 0
        v.g['rv-btn'] = 1
        eq(v.call('ride-set-park', 0), 7)
        v.g['rv-btn'] = 0
        eq(v.call('ride-set-park', 0), 0)
        eq(v.call('ride-set-park', 1), 11)
        v.adc[1] = .2
        eq(v.call('ride-set-park', 1), 0)
    case('P exit/entry standstill throttle reverse brake guards', park_guards)

    def park_motor(v):
        v.adc[0] = .9
        v.g['pas-amps'] = 20
        v.call('motor-control-step')
        eq(v.output[-1], ('set-current', 0))
        eq(v.g['pas-amps'], 0)
        v.adc[1] = .5
        v.call('motor-control-step')
        eq(v.output[-1][0], 'set-brake-rel')
    case('P blocks throttle/PAS but permits deliberate brake', park_motor)

    def stale(v):
        v.g.update({'park-on': 0, 'throttle-on': 1, 'tx-live': 1, 'tx-seen': 0})
        v.call('motor-control-step')
        eq(v.g['safety-fault'], 12)
        eq(v.output[-1], ('set-current', 0))
    case('stale mode monitor forces P and zero motor output', stale)

    def rx_stale(v):
        v.g.update({'park-on': 0, 'throttle-on': 1, 'rm-rev-en': 1,
                    'rv-hw-ok': 1, 'rv-seen': 0, 'rv-armed': 1,
                    'rv-dir': -1, 'rv-rel': .8})
        v.call('motor-control-step')
        eq(v.g['park-on'], 1)
        eq(v.g['rv-armed'], 0)
        eq(v.output[-1], ('set-current', 0))
    case('stale RX monitor revokes reverse and propulsion', rx_stale)

    def native(v):
        v.config['adc-ctrl-type'] = 1
        v.g.update({'park-on': 0, 'throttle-on': 1})
        v.adc[0] = .8
        v.call('motor-control-step')
        eq(v.g['park-on'], 1)
        eq(v.output[-1], ('set-current', 0))
        eq(v.call('ride-set-park', 0), 6)
        v.adc[0] = 0
        eq(v.call('ride-set-park', 0), 12)
    case('native control enabled latches fault and refuses P exit', native)

    def adc_fault(v):
        v.native_ok = False
        v.call('motor-control-step')
        eq(v.g['safety-fault'], 12)
        eq(v.output[-1], ('set-current', 0))
    case('ADC range fault stops propulsion', adc_fault)

    def pas(v):
        v.call('ride-pas-input', 10, 0)
        v.call('ride-set-park', 0)
        v.call('ride-pas-input', 10, 20)
        eq(v.g['pas-amps'], 0)
        v.call('ride-pas-input', 10, 0)
        v.call('ride-pas-input', 11, 20)
        eq(v.g['pas-amps'], 0)
        v.call('ride-pas-input', 10, 20)
        eq(v.g['pas-amps'], 20)
        v.now += .5
        v.call('motor-control-step')
        eq(v.output[-1], ('set-current', 0))
    case('PAS requires same-source fresh zero and expires', pas)

    def reverse(v):
        v.g.update({'park-on': 0, 'rm-rev-en': 1, 'rv-hw-ok': 1,
                    'rv-seen-release': 1, 'rv-btn': 1})
        v.speed = -.2
        v.call('reverse-step')
        eq(v.g['rv-armed'], 0)
        v.speed = 0
        v.adc[1] = .5
        for _ in range(20):
            v.call('reverse-step')
        eq(v.g['rv-armed'], 1)
        v.adc[1] = 0
        v.call('reverse-step')
        eq(v.g['rv-dir'], -1)
        v.speed = -1
        v.call('reverse-step')
        eq(v.g['rv-dir'], -1)
        v.g['rv-btn'] = 0
        v.call('reverse-step')
        eq(v.g['rv-dir'], 0)
        v.speed = 0
        v.call('reverse-step')
        eq(v.g['rv-dir'], 1)
    case('reverse standstill arm, moving reverse hold, release interlock', reverse)

    def button(v):
        def tick(raw, count):
            for _ in range(count):
                v.now += .01
                v.g['motor-seen'] = v.now
                v.call('mode-button-step', raw)
        tick(0, 120)
        eq(v.g['park-on'], 1)
        tick(1, 6)
        tick(0, 6)
        tick(1, 6)
        eq(v.g['park-on'], 0)
        tick(0, 6)
        tick(1, 6)
        eq(v.g['current-profile'], 1)
        v.adc[1] = .5
        tick(0, 120)
        tick(1, 6)
        eq(v.g['park-on'], 1)
        eq(v.g['current-profile'], 1)
        eq(v.output, [])
    case('MODE boot held, short exit/select, long P no selection or torque', button)

    def clamp(v):
        v.g.update({'park-on': 0, 'throttle-on': 1})
        v.call('ride-select-mode', 2)
        eq(v.config['l-current-max-scale'], 1)
        v.call('ride-select-mode', 0)
        eq(v.config['l-current-max-scale'], 50/70)
        eq(v.output, [])
    case('independent mode ceilings clamp to ESC, no motor command', clamp)

    def commands(v):
        sent = []
        v.ops['safety-send'] = lambda *args: sent.append(args)
        v.call('panel-set-throttle', 1)
        eq(v.g['park-on'], 1)
        v.call('safety-query', 12, 100)
        v.call('safety-set', 13, 100, 0)
        eq(v.g['park-on'], 1)
        eq(sent[-1][-1], 14)
        v.call('safety-set', 12, 100, 0)
        eq(v.g['park-on'], 0)
        v.adc[1] = .5
        v.call('ride-set-park', 1)
        v.call('safety-set', 12, 100, 0)
        eq(v.g['park-on'], 1)
        v.call('safety-query', 12, 101)
        v.now += 1.01
        v.call('safety-set', 12, 101, 0)
        eq(v.g['park-on'], 1)
        eq(sent[-1][-1], 14)
    case('legacy unlock denied; safety command owner/one-shot/expiry guards', commands)
    def wire_size(v):
        v.call('safety-send', 12, 0x1234, 0x8B, 0)
        eq(v.sent[-1], bytes.fromhex('56508b011234000000'))
        v.g['park-on'] = 0
        v.g['current-profile'] = 2
        v.call('safety-send', 12, 0xFFFF, 0x8C, 14)
        eq(v.sent[-1], bytes.fromhex('56508c01ffff01020e'))
    case('safety reply exact 9-byte payload, sequence/state/result offsets', wire_size)
    def status_wire(v):
        v.call('rm-send-status-seq', 12, 0x1234)
        eq(v.sent[-1], bytes.fromhex('56508d123400000001f401f402bc0100000000'))
        v.g.update({'current-profile': 2, 'rv-dir': -1, 'rv-btn': 1, 'rv-armed': 1})
        v.call('rm-send-status-seq', 12, 0xFFFF)
        eq(v.sent[-1], bytes.fromhex('56508dffff00000203e802bc02bcff01010000'))
    case('sequenced status exact 19-byte payload and clamped-current offsets', status_wire)

    def fault_reporting(v):
        # A blocked mode is reproducible with a latched input fault. Polls must
        # report that cause even if the most recent command succeeded.
        v.call('ride-input-fault')
        v.call('ride-select-mode', 1)
        eq(v.g['current-profile'], 0)
        v.call('safety-query', 12, 0x1234)
        eq(v.sent[-1][6:], bytes([5, 0, 12]))
        v.call('rm-send-status-seq', 12, 0x1234)
        eq(v.sent[-1][18], 12)
        v.call('rm-send-status', 12)
        eq(v.sent[-1][16], 12)
    case('latched input fault blocks mode and remains visible in all polls', fault_reporting)

    def boot_fault_reporting(v):
        v.g.update({'safety-fault': 9, 'rm-fault': 0})
        v.call('safety-query', 12, 1)
        eq(v.sent[-1][6:], bytes([5, 0, 9]))
    case('unsupported native input at boot reports cause 9 instead of OK', boot_fault_reporting)

    def missing_motor_reporting(v):
        v.g.update({'motor-live': 0, 'safety-fault': 0, 'rm-fault': 0})
        v.call('safety-query', 12, 1)
        eq(v.sent[-1][6:], bytes([5, 0, 12]))
        v.g.update({'motor-live': 1, 'motor-seen': v.now - .2})
        v.call('safety-query', 12, 2)
        eq(v.sent[-1][6:], bytes([5, 0, 12]))
    case('missing or stale motor worker reports input fault before supervisor', missing_motor_reporting)

    def healthy_reporting(v):
        v.g['rm-fault'] = 11
        v.call('safety-query', 12, 1)
        eq(v.sent[-1][6:], bytes([0, 0, 11]))
        v.call('ride-select-mode', 2)
        eq(v.g['current-profile'], 2)
        eq(v.g['park-on'], 1)
        eq(v.output, [])
    case('healthy PARK permits panel modes and preserves command refusal', healthy_reporting)
    print(f'{sum(results)}/{len(results)} host branch cases passed; NOT hardware or LispBM validation')
    return all(results)


def check_startup_order(forms):
    definitions = {f[1]: f for f in forms
                   if isinstance(f, list) and f and f[0] == 'defun'}
    bound = set()

    def visit(expr, seen):
        if not isinstance(expr, list) or not expr:
            return
        if expr[0] in ('quote', 'defun'):
            return
        if isinstance(expr[0], Symbol) and expr[0] in definitions:
            name = expr[0]
            if name not in bound:
                raise AssertionError(f'startup reaches unbound function {name}')
            if name not in seen:
                for body in definitions[name][3:]:
                    visit(body, seen | {name})
        for child in expr[1:]:
            visit(child, seen)
    for form in forms:
        if isinstance(form, list) and form and form[0] == 'defun':
            bound.add(form[1])
        else:
            visit(form, set())
    print('PASS static startup direct-call definition order (not scheduler validation)')


if __name__ == '__main__':
    import argparse
    import sys
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'lisp' / 'main.lisp')
    source = parser.parse_args().source
    try:
        forms = parse(source.read_text(encoding='utf-8-sig'))
        print(f'PASS full-source delimiter parse ({len(forms)} top-level forms)')
        check_startup_order(forms)
        sys.exit(0 if run(forms) else 1)
    except SyntaxError as error:
        print('FAIL full-source delimiter parse:', error)
        sys.exit(1)
