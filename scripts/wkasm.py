#!/usr/bin/env python3
"""wkasm - the WinKit assembler.

A small, dependency-free 8086 assembler written in Python. It understands
the MASM-style dialect WinKit's sources use, produces flat binaries, and is
meant to be read: everything the build needs is in this one file.

    python scripts/wkasm.py src/GUI.ASM -o out/bin/KERNEL.BIN
    python scripts/wkasm.py src/progs/HELLO.ASM -o HELLO.WKX -m

Output is a flat binary that starts at the first ORG (0 if there is none),
so a program written with ORG 100h becomes a file whose first byte is the
byte at offset 100h. See docs/WKX-PROGRAMMING.md for the dialect.

Supported
---------
* Instructions: the 8086 integer set that WinKit uses (data movement, ALU,
  shifts by 1 or CL, mul/div, jumps, calls, string ops, in/out, int, flags).
* Directives: ORG, EQU, DB, DW, DD (with DUP and ?), MACRO/ENDM/LOCAL,
  INCLUDE, PROC/ENDP, SEGMENT/ENDS/ASSUME/END (accepted, no effect).
* Expressions: + - * / MOD SHL SHR AND OR XOR NOT, parentheses, OFFSET,
  HIGH, LOW, $, numbers (decimal, 0FFh, 0x1F), character constants.
* Operands: registers, immediates, [base+index+disp], symbol[reg],
  seg:[...], BYTE/WORD PTR, and variables (typed by their DB/DW).
* Case-insensitive names, like MASM.

Encodings follow MASM 5.10 (same opcode forms, forward JMP padded with a
NOP) so the output can be compared byte for byte with MASM's.
"""
import argparse
import os
import re
import sys

REG16 = {"ax": 0, "cx": 1, "dx": 2, "bx": 3, "sp": 4, "bp": 5, "si": 6, "di": 7}
REG8 = {"al": 0, "cl": 1, "dl": 2, "bl": 3, "ah": 4, "ch": 5, "dh": 6, "bh": 7}
SREG = {"es": 0, "cs": 1, "ss": 2, "ds": 3}
SEG_PREFIX = {"es": 0x26, "cs": 0x2E, "ss": 0x36, "ds": 0x3E}
BASES = ("bx", "bp")
INDEXES = ("si", "di")

CONDITIONS = {
    "jo": 0x70, "jno": 0x71, "jb": 0x72, "jc": 0x72, "jnae": 0x72,
    "jae": 0x73, "jnb": 0x73, "jnc": 0x73, "je": 0x74, "jz": 0x74,
    "jne": 0x75, "jnz": 0x75, "jbe": 0x76, "jna": 0x76, "ja": 0x77,
    "jnbe": 0x77, "js": 0x78, "jns": 0x79, "jp": 0x7A, "jpe": 0x7A,
    "jnp": 0x7B, "jpo": 0x7B, "jl": 0x7C, "jnge": 0x7C, "jge": 0x7D,
    "jnl": 0x7D, "jle": 0x7E, "jng": 0x7E, "jg": 0x7F, "jnle": 0x7F,
}
ALU = {"add": 0, "or": 1, "adc": 2, "sbb": 3, "and": 4, "sub": 5, "xor": 6,
       "cmp": 7}
SHIFT = {"rol": 0, "ror": 1, "rcl": 2, "rcr": 3, "shl": 4, "sal": 4,
         "shr": 5, "sar": 7}
UNARY = {"not": 2, "neg": 3, "mul": 4, "imul": 5, "div": 6, "idiv": 7}
SIMPLE = {
    "cli": 0xFA, "sti": 0xFB, "cld": 0xFC, "std": 0xFD, "clc": 0xF8,
    "stc": 0xF9, "cmc": 0xF5, "hlt": 0xF4, "nop": 0x90, "ret": 0xC3,
    "retf": 0xCB, "iret": 0xCF, "lodsb": 0xAC, "lodsw": 0xAD,
    "stosb": 0xAA, "stosw": 0xAB, "movsb": 0xA4, "movsw": 0xA5,
    "cmpsb": 0xA6, "cmpsw": 0xA7, "scasb": 0xAE, "scasw": 0xAF,
    "pushf": 0x9C, "popf": 0x9D, "cbw": 0x98, "cwd": 0x99, "sahf": 0x9E,
    "lahf": 0x9F, "xlat": 0xD7, "into": 0xCE,
}
PREFIXES = {"rep": 0xF3, "repe": 0xF3, "repz": 0xF3, "repne": 0xF2,
            "repnz": 0xF2, "lock": 0xF0}
IDENT = r"[A-Za-z_@?$][\w@?$]*"


class AsmError(Exception):
    pass


class Unresolved(Exception):
    """A symbol has no value yet (first pass)."""


# --------------------------------------------------------------------------
# Source loading, comments, INCLUDE, MACRO
# --------------------------------------------------------------------------

def strip_comment(line):
    quote = None
    for i, ch in enumerate(line):
        if quote:
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
        elif ch == ";":
            return line[:i]
    return line


def find_file(directory, name):
    path = os.path.join(directory, name)
    if os.path.exists(path):
        return path
    low = name.lower()
    for entry in os.listdir(directory):
        if entry.lower() == low:
            return os.path.join(directory, entry)
    raise AsmError(f"cannot find include file {name}")


def load_lines(path, depth=0):
    """Return [(text, file, line number)], with INCLUDEs expanded."""
    if depth > 16:
        raise AsmError(f"{path}: includes nested too deeply")
    out = []
    directory = os.path.dirname(os.path.abspath(path))
    with open(path, encoding="latin-1") as fh:
        for number, raw in enumerate(fh, 1):
            text = strip_comment(raw.rstrip("\r\n")).strip()
            m = re.match(r"(?i)^include\s+(\S+)\s*$", text)
            if m:
                inc = find_file(directory, m.group(1).strip("'\""))
                out += load_lines(inc, depth + 1)
            else:
                out.append((text, os.path.basename(path), number))
    return out


def split_top(text, sep=","):
    """Split on sep outside quotes, parentheses and brackets."""
    parts, depth, quote, cur = [], 0, None, []
    for ch in text:
        if quote:
            cur.append(ch)
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
            cur.append(ch)
        elif ch in "([":
            depth += 1
            cur.append(ch)
        elif ch in ")]":
            depth -= 1
            cur.append(ch)
        elif ch == sep and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    tail = "".join(cur).strip()
    if tail or parts:
        parts.append(tail)
    return parts


class Macro:
    def __init__(self, params, body):
        self.params = params
        self.body = body


def expand_macros(lines):
    """Collect MACRO definitions and expand their invocations."""
    macros = {}
    rest = []
    i = 0
    while i < len(lines):
        text, f, n = lines[i]
        m = re.match(rf"(?i)^({IDENT})\s+macro\b(.*)$", text)
        if m:
            params = [p.strip().lower() for p in m.group(2).split(",") if p.strip()]
            body = []
            i += 1
            while i < len(lines) and not re.match(r"(?i)^endm\b", lines[i][0]):
                body.append(lines[i])
                i += 1
            if i >= len(lines):
                raise AsmError(f"{f}({n}): macro {m.group(1)} has no ENDM")
            macros[m.group(1).lower()] = Macro(params, body)
        else:
            rest.append((text, f, n))
        i += 1
    counter = [0]

    def expand(line_list, depth):
        if depth > 12:
            raise AsmError("macros nested too deeply")
        out = []
        for text, f, n in line_list:
            m = re.match(rf"^(?:({IDENT}):\s*)?({IDENT})\b\s*(.*)$", text)
            if m and m.group(2).lower() in macros:
                label, name, argtext = m.group(1), m.group(2).lower(), m.group(3)
                macro = macros[name]
                args = split_top(argtext) if argtext.strip() else []
                if len(args) < len(macro.params):
                    raise AsmError(f"{f}({n}): {name} needs "
                                   f"{len(macro.params)} arguments")
                if label:
                    out.append((label + ":", f, n))
                counter[0] += 1
                subst = {p: a for p, a in zip(macro.params, args)}
                locals_ = {}
                body = []
                for btext, bf, bn in macro.body:
                    lm = re.match(r"(?i)^local\s+(.*)$", btext)
                    if lm:
                        for nm in lm.group(1).split(","):
                            locals_[nm.strip().lower()] = f"??{counter[0]}_{nm.strip()}"
                        continue
                    body.append((btext, bf, bn))
                names = sorted(list(subst) + list(locals_), key=len, reverse=True)
                pattern = (re.compile(r"\b(" + "|".join(map(re.escape, names)) + r")\b",
                                      re.I) if names else None)

                def sub(mo):
                    key = mo.group(1).lower()
                    return locals_[key] if key in locals_ else subst[key]

                expanded = []
                for btext, bf, bn in body:
                    t = btext
                    if pattern:
                        # do not touch text inside quotes
                        t = "".join(seg if seg[:1] in "'\"" else pattern.sub(sub, seg)
                                    for seg in re.split(r"('(?:[^']|'')*'|\"[^\"]*\")", t))
                    expanded.append((t, f, n))
                out += expand(expanded, depth + 1)
            else:
                out.append((text, f, n))
        return out

    return expand(rest, 0)


# --------------------------------------------------------------------------
# Expressions
# --------------------------------------------------------------------------

TOKEN = re.compile(r"""\s*(?:
    (?P<str>'(?:[^']|'')*'|"(?:[^"]|"")*")|
    (?P<num>0[xX][0-9A-Fa-f]+|[0-9][0-9A-Fa-f]*[hH](?![\w])|[0-9]+(?![\w]))|
    (?P<id>[A-Za-z_@?$][\w@?$]*)|
    (?P<op>[-+*/()])
)""", re.X)

WORD_OPS = {"mod", "shl", "shr", "and", "or", "xor", "not", "offset", "high",
            "low", "ptr", "short", "near", "far", "byte", "word", "dword"}


def tokenize(text):
    pos, out = 0, []
    text = text.strip()
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m or m.end() == pos:
            raise AsmError(f"cannot parse {text[pos:]!r}")
        pos = m.end()
        if m.group("str") is not None:
            out.append(("str", m.group("str")))
        elif m.group("num") is not None:
            out.append(("num", m.group("num")))
        elif m.group("id") is not None:
            out.append(("id", m.group("id").lower()))
        else:
            out.append(("op", m.group("op")))
    return out


def parse_number(s):
    s = s.lower()
    if s.startswith("0x"):
        return int(s[2:], 16)
    if s.endswith("h"):
        return int(s[:-1], 16)
    return int(s)


class ExprParser:
    def __init__(self, tokens):
        self.t = tokens
        self.i = 0

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else (None, None)

    def take(self):
        tok = self.peek()
        self.i += 1
        return tok

    def parse(self):
        node = self.or_()
        if self.i != len(self.t):
            raise AsmError(f"unexpected {self.peek()[1]!r} in expression")
        return node

    def _binary(self, sub, ops):
        node = sub()
        while True:
            kind, val = self.peek()
            if (kind == "op" or kind == "id") and val in ops:
                self.take()
                node = ("bin", val, node, sub())
            else:
                return node

    def or_(self):
        return self._binary(self.and_, ("or", "xor"))

    def and_(self):
        return self._binary(self.add, ("and",))

    def add(self):
        return self._binary(self.mul, ("+", "-"))

    def mul(self):
        return self._binary(self.unary, ("*", "/", "mod", "shl", "shr"))

    def unary(self):
        kind, val = self.peek()
        if kind == "op" and val in "+-":
            self.take()
            node = self.unary()
            return ("neg", node) if val == "-" else node
        if kind == "id" and val in ("not", "offset", "high", "low"):
            self.take()
            return (val, self.unary())
        return self.primary()

    def primary(self):
        kind, val = self.take()
        if kind == "num":
            return ("num", parse_number(val))
        if kind == "str":
            body = val[1:-1].replace("''", "'").replace('""', '"')
            value = 0
            for ch in body.encode("latin-1"):
                value = (value << 8) | ch
            return ("num", value)
        if kind == "id":
            if val in WORD_OPS:
                raise AsmError(f"unexpected {val!r} in expression")
            return ("loc",) if val == "$" else ("sym", val)
        if kind == "op" and val == "(":
            node = self.or_()
            if self.take() != ("op", ")"):
                raise AsmError("missing )")
            return node
        raise AsmError("incomplete expression")


def parse_expr(text):
    return ExprParser(tokenize(text)).parse()


def ast_symbols(node, under_offset=False):
    """Yield (name, under_offset) for every symbol reference."""
    if node[0] == "sym":
        yield node[1], under_offset
    elif node[0] in ("neg", "not", "high", "low"):
        yield from ast_symbols(node[1], under_offset)
    elif node[0] == "offset":
        yield from ast_symbols(node[1], True)
    elif node[0] == "bin":
        yield from ast_symbols(node[2], under_offset)
        yield from ast_symbols(node[3], under_offset)


# --------------------------------------------------------------------------
# Statements
# --------------------------------------------------------------------------

class Operand:
    """A parsed (not yet classified) operand."""

    def __init__(self, kind, **kw):
        self.kind = kind                # 'reg' | 'expr' | 'mem'
        self.reg = None
        self.ast = None
        self.seg = None
        self.size = None
        self.base = None
        self.index = None
        self.disp = None
        self.__dict__.update(kw)


def split_terms(text):
    """'bx+si-4' -> [(+,'bx'), (+,'si'), (-,'4')] (top level only)."""
    terms, sign, cur, depth, quote = [], "+", [], 0, None
    for ch in text:
        if quote:
            cur.append(ch)
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
            cur.append(ch)
        elif ch == "(":
            depth += 1
            cur.append(ch)
        elif ch == ")":
            depth -= 1
            cur.append(ch)
        elif ch in "+-" and depth == 0:
            if "".join(cur).strip():
                terms.append((sign, "".join(cur).strip()))
                cur = []
            sign = ch
        else:
            cur.append(ch)
    if "".join(cur).strip():
        terms.append((sign, "".join(cur).strip()))
    return terms


def parse_operand(text):
    t = text.strip()
    size = seg = None
    m = re.match(r"(?i)^(byte|word|dword)\s+ptr\s+(.*)$", t)
    if m:
        size = {"byte": "b", "word": "w", "dword": "d"}[m.group(1).lower()]
        t = m.group(2).strip()
    m = re.match(r"(?i)^(es|cs|ss|ds)\s*:\s*(.*)$", t)
    if m:
        seg = m.group(1).lower()
        t = m.group(2).strip()
    low = t.lower()
    if size is None and seg is None and (low in REG16 or low in REG8 or low in SREG):
        return Operand("reg", reg=low)
    if "[" in t:
        i, j = t.index("["), t.rindex("]")
        outside = (t[:i] + t[j + 1:]).strip()
        base = index = None
        disp = None
        pieces = []
        if outside:
            pieces.append(("+", outside))
        pieces += split_terms(t[i + 1:j])
        for sign, term in pieces:
            lt = term.lower()
            if lt in BASES or lt in INDEXES:
                if sign != "+":
                    raise AsmError(f"cannot subtract a register: {text}")
                if lt in BASES:
                    if base:
                        raise AsmError(f"two base registers: {text}")
                    base = lt
                else:
                    if index:
                        raise AsmError(f"two index registers: {text}")
                    index = lt
            else:
                node = parse_expr(term)
                if sign == "-":
                    node = ("neg", node)
                disp = node if disp is None else ("bin", "+", disp, node)
        return Operand("mem", seg=seg, size=size, base=base, index=index,
                       disp=disp)
    node = parse_expr(t)
    if seg is not None or size is not None:
        return Operand("mem", seg=seg, size=size, disp=node)
    return Operand("expr", ast=node)


class Statement:
    def __init__(self, kind, where, **kw):
        self.kind = kind
        self.where = where              # "file(line)"
        self.__dict__.update(kw)


def parse_statements(lines):
    stmts = []
    for text, f, n in lines:
        where = f"{f}({n})"
        if not text:
            continue
        try:
            stmts += parse_line(text, where)
        except AsmError as e:
            raise AsmError(f"{where}: {e}") from None
    return stmts


def parse_line(text, where):
    out = []
    m = re.match(rf"^({IDENT}):(?!:)\s*(.*)$", text)
    if m and m.group(1).lower() not in SREG:
        out.append(Statement("label", where, name=m.group(1).lower()))
        text = m.group(2)
        if not text:
            return out
    words = text.split(None, 2)
    first = words[0].lower()
    second = words[1].lower() if len(words) > 1 else ""
    rest = words[2] if len(words) > 2 else ""
    if second in ("db", "dw", "dd", "equ", "proc", "endp", "segment", "ends",
                  "macro") and first not in ("db", "dw", "dd", "equ"):
        if second == "equ":
            out.append(Statement("equ", where, name=first, ast=parse_expr(rest)))
        elif second in ("db", "dw", "dd"):
            out.append(Statement("data", where, name=first, size=second,
                                 items=parse_data(rest)))
        elif second == "proc":
            out.append(Statement("label", where, name=first, proc=True))
        return out                      # endp / segment / ends: nothing
    if first in ("db", "dw", "dd"):
        body = text.split(None, 1)[1] if len(words) > 1 else ""
        out.append(Statement("data", where, name=None, size=first,
                             items=parse_data(body)))
        return out
    if first == "org":
        out.append(Statement("org", where, ast=parse_expr(text.split(None, 1)[1])))
        return out
    if first in ("assume", "end", "title", "name", "page"):
        return out
    if first in ("local", "endm"):
        return out
    prefix = []
    rest_text = text
    while first in PREFIXES:
        prefix.append(PREFIXES[first])
        rest_text = rest_text.split(None, 1)[1] if len(rest_text.split(None, 1)) > 1 else ""
        words = rest_text.split(None, 1)
        if not words:
            raise AsmError(f"{first} needs an instruction")
        first = words[0].lower()
    parts = rest_text.split(None, 1)
    mnem = parts[0].lower()
    form = "auto"
    operand_text = parts[1] if len(parts) > 1 else ""
    if mnem in ("jmp", "call"):
        m = re.match(r"(?i)^(short|near|far)\s+(?:ptr\s+)?(.*)$", operand_text.strip())
        if m:
            form = "short" if m.group(1).lower() == "short" else "near"
            operand_text = m.group(2)
    operands = []
    if operand_text.strip():
        operands = [parse_operand(o) for o in split_top(operand_text)]
    out.append(Statement("instr", where, mnem=mnem, ops=operands, prefix=prefix,
                         text=text, jmp_form=form))
    return out


def parse_data(text):
    items = []
    for part in split_top(text):
        if not part:
            continue
        m = re.match(r"(?i)^(.*?)\s+dup\s*\((.*)\)\s*$", part)
        if m:
            items.append(("dup", parse_expr(m.group(1)), parse_data(m.group(2))))
        elif part == "?":
            items.append(("undef",))
        elif re.match(r"^'(?:[^']|'')*'$", part) or re.match(r'^"[^"]*"$', part):
            body = part[1:-1].replace("''", "'")
            items.append(("str", body.encode("latin-1")))
        else:
            items.append(("expr", parse_expr(part)))
    return items


# --------------------------------------------------------------------------
# The assembler proper
# --------------------------------------------------------------------------

class Assembler:
    def __init__(self, stmts):
        self.stmts = stmts
        self.kinds = {}                 # symbol -> equ | db | dw | dd | label
        self.values = {}                # final / previous-pass values
        self.origin = None
        for s in stmts:
            if s.kind == "equ":
                self.kinds[s.name] = "equ"
            elif s.kind == "data" and s.name:
                self.kinds[s.name] = s.size
            elif s.kind == "label":
                self.kinds[s.name] = "label"

    # ---- expression evaluation ----
    def eval(self, node):
        k = node[0]
        if k == "num":
            return node[1]
        if k == "loc":
            return self.pc
        if k == "sym":
            name = node[1]
            if name in self.values:
                return self.values[name]
            if self.final:
                raise AsmError(f"undefined symbol {name}")
            self.unknown = True
            raise Unresolved(name)
        if k == "neg":
            return -self.eval(node[1])
        if k == "not":
            return ~self.eval(node[1])
        if k == "offset":
            return self.eval(node[1])
        if k == "high":
            return (self.eval(node[1]) >> 8) & 0xFF
        if k == "low":
            return self.eval(node[1]) & 0xFF
        op, a, b = node[1], self.eval(node[2]), self.eval(node[3])
        if op == "+":
            return a + b
        if op == "-":
            return a - b
        if op == "*":
            return a * b
        if op == "/":
            return a // b if b else 0
        if op == "mod":
            return a % b if b else 0
        if op == "shl":
            return a << b
        if op == "shr":
            return (a & 0xFFFFFFFF) >> b
        if op == "and":
            return a & b
        if op == "or":
            return a | b
        return a ^ b

    def value(self, node):
        """(value, known). Unknown symbols give 0 in the sizing pass."""
        try:
            return self.eval(node), True
        except Unresolved:
            return 0, False

    # ---- operand classification ----
    def data_size(self, node):
        for name, under in ast_symbols(node):
            if not under and self.kinds.get(name) in ("db", "dw", "dd"):
                return {"db": "b", "dw": "w", "dd": "d"}[self.kinds[name]]
        return None

    def classify(self, op):
        """-> dict(kind=r8|r16|sr|imm|mem, ...)"""
        if op.kind == "reg":
            if op.reg in REG16:
                return dict(kind="r16", num=REG16[op.reg], name=op.reg)
            if op.reg in REG8:
                return dict(kind="r8", num=REG8[op.reg], name=op.reg)
            return dict(kind="sr", num=SREG[op.reg], name=op.reg)
        if op.kind == "expr":
            size = self.data_size(op.ast)
            is_mem = any(not under and self.kinds.get(n) in ("db", "dw", "dd")
                         for n, under in ast_symbols(op.ast))
            if not is_mem:
                v, known = self.value(op.ast)
                return dict(kind="imm", val=v, known=known)
            return dict(kind="mem", seg=None, base=None, index=None,
                        disp=op.ast, size=size)
        size = op.size or (self.data_size(op.disp) if op.disp is not None else None)
        return dict(kind="mem", seg=op.seg, base=op.base, index=op.index,
                    disp=op.disp, size=size)

    @staticmethod
    def op_size(*ops):
        for o in ops:
            if o["kind"] == "r8":
                return "b"
            if o["kind"] == "r16":
                return "w"
        for o in ops:
            if o["kind"] == "mem" and o.get("size"):
                return "w" if o["size"] == "d" else o["size"]
        return None

    # ---- encoders ----
    def modrm(self, regfield, op):
        if op["kind"] in ("r8", "r16"):
            return bytes([0xC0 | (regfield << 3) | op["num"]])
        base, index = op["base"], op["index"]
        v, known = (self.value(op["disp"]) if op["disp"] is not None else (0, True))
        v &= 0xFFFF
        sv = v - 0x10000 if v >= 0x8000 else v
        if base is None and index is None:
            return bytes([(regfield << 3) | 6, v & 0xFF, v >> 8])
        table = {("bx", "si"): 0, ("bx", "di"): 1, ("bp", "si"): 2,
                 ("bp", "di"): 3, (None, "si"): 4, (None, "di"): 5,
                 ("bp", None): 6, ("bx", None): 7}
        rm = table[(base, index)]
        if op["disp"] is None or (known and v == 0 and rm != 6):
            if rm == 6:
                return bytes([0x40 | (regfield << 3) | rm, 0])
            return bytes([(regfield << 3) | rm])
        if known and -128 <= sv <= 127:
            return bytes([0x40 | (regfield << 3) | rm, sv & 0xFF])
        return bytes([0x80 | (regfield << 3) | rm, v & 0xFF, v >> 8])

    def seg_prefix(self, *ops):
        for o in ops:
            if o["kind"] == "mem" and o["seg"]:
                return bytes([SEG_PREFIX[o["seg"]]])
        return b""

    @staticmethod
    def fits8(v):
        v &= 0xFFFF
        return v < 0x80 or v >= 0xFF80

    def imm_bytes(self, val, size):
        if size == "b":
            if not -128 <= val <= 255:
                raise AsmError(f"byte value {val} out of range")
            return bytes([val & 0xFF])
        if not -32768 <= val <= 65535:
            raise AsmError(f"word value {val} out of range")
        return bytes([val & 0xFF, (val >> 8) & 0xFF])

    def direct(self, op):
        return op["kind"] == "mem" and op["base"] is None and op["index"] is None

    def rel8(self, target, end):
        d = target - end
        if not -128 <= d <= 127:
            raise AsmError(f"jump out of range by {abs(d) - (128 if d < 0 else 127)} "
                           "byte(s)")
        return bytes([d & 0xFF])

    def jump_target(self, op):
        if op.kind != "expr":
            return None
        return op.ast

    def encode(self, st, pc, final):
        m = st.mnem
        pre = bytes(st.prefix)
        if m in SIMPLE and not st.ops:
            return pre + bytes([SIMPLE[m]])
        if m == "int":
            v, _ = self.value(st.ops[0].ast)
            return b"\xCC" if v == 3 else bytes([0xCD, v & 0xFF])
        # control flow with labels
        if m in CONDITIONS or m in ("loop", "loope", "loopne", "jcxz", "jmp",
                                    "call"):
            return pre + self.encode_flow(st, pc, final)
        ops = [self.classify(o) for o in st.ops]
        if m == "mov":
            return pre + self.mov(ops, st)
        if m in ALU:
            return pre + self.alu(ALU[m], ops, st)
        if m == "test":
            return pre + self.test(ops, st)
        if m in ("inc", "dec"):
            return pre + self.incdec(m == "dec", ops[0])
        if m in UNARY:
            return pre + self.unary(UNARY[m], ops[0])
        if m in SHIFT:
            return pre + self.shift(SHIFT[m], ops, st)
        if m == "push":
            return pre + self.push(ops[0])
        if m == "pop":
            return pre + self.pop(ops[0])
        if m in ("in", "out"):
            return pre + self.inout(m, ops)
        if m == "xchg":
            return pre + self.xchg(ops)
        if m == "lea":
            return pre + b"\x8D" + self.modrm(ops[0]["num"], ops[1])
        if m in ("les", "lds"):
            return pre + bytes([0xC4 if m == "les" else 0xC5]) + \
                self.seg_prefix(ops[1]) + self.modrm(ops[0]["num"], ops[1])
        raise AsmError(f"unknown instruction {m!r}")

    def encode_flow(self, st, pc, final):
        m = st.mnem
        op = st.ops[0]
        if op.kind == "expr":
            ast = op.ast
        else:
            ast = None
        if m in CONDITIONS or m in ("loop", "loope", "loopne", "jcxz"):
            opcode = CONDITIONS.get(m) or {"loop": 0xE2, "loope": 0xE1,
                                          "loopne": 0xE0, "jcxz": 0xE3}[m]
            if ast is None:
                raise AsmError(f"{m} needs a label")
            target, _ = self.value(ast)
            if not final:
                return bytes([opcode, 0])
            return bytes([opcode]) + self.rel8(target, pc + 2)
        if ast is None:                  # jmp/call through a register or memory
            o = self.classify(op)
            ext = 4 if m == "jmp" else 2
            if o["kind"] == "mem":
                return self.seg_prefix(o) + b"\xFF" + self.modrm(ext, o)
            return b"\xFF" + self.modrm(ext, o)
        target, known = self.value(ast)

        def near():
            return b"\xE9" + ((target - (pc + 3)) & 0xFFFF).to_bytes(2, "little")

        if m == "call":
            if not final:
                return b"\xE8\0\0"
            return b"\xE8" + ((target - (pc + 3)) & 0xFFFF).to_bytes(2, "little")
        form = st.jmp_form
        if form == "short":
            return b"\xEB" + (self.rel8(target, pc + 2) if final else b"\0")
        if form == "near":
            return near() if final else b"\xE9\0\0"
        # Plain JMP, sized like MASM. In the first pass a backward target
        # that is in range is short; everything else (including every
        # forward reference) is near. In the second pass a forward jump that
        # turns out to be in range is emitted short, padded with a NOP.
        d = target - (pc + 2)
        if not final:
            return b"\xEB\0" if known and -128 <= d <= 127 else b"\xE9\0\0"
        if st.size_hint == 2:
            return b"\xEB" + self.rel8(target, pc + 2)
        if -128 <= d <= 127:
            return b"\xEB" + bytes([d & 0xFF, 0x90])
        return near()

    def mov(self, ops, st):
        d, s = ops
        if d["kind"] == "sr":
            if s["kind"] == "r16":
                return b"\x8E" + self.modrm(d["num"], s)
            if s["kind"] == "mem":
                return self.seg_prefix(s) + b"\x8E" + self.modrm(d["num"], s)
            raise AsmError("bad segment register load")
        if s["kind"] == "sr":
            return self.seg_prefix(d) + b"\x8C" + self.modrm(s["num"], d)
        size = self.op_size(d, s)
        if size is None:
            raise AsmError("operand size is ambiguous (use BYTE PTR or WORD PTR)")
        w = 1 if size == "w" else 0
        if s["kind"] == "imm":
            if d["kind"] == "r8":
                return bytes([0xB0 + d["num"]]) + self.imm_bytes(s["val"], "b")
            if d["kind"] == "r16":
                return bytes([0xB8 + d["num"]]) + self.imm_bytes(s["val"], "w")
            return self.seg_prefix(d) + bytes([0xC6 + w]) + self.modrm(0, d) \
                + self.imm_bytes(s["val"], size)
        if d["kind"] in ("r8", "r16") and s["kind"] == "mem":
            if d["num"] == 0 and self.direct(s):
                v, _ = self.value(s["disp"])
                return self.seg_prefix(s) + bytes([0xA0 + w, v & 0xFF,
                                                   (v >> 8) & 0xFF])
            return self.seg_prefix(s) + bytes([0x8A + w]) + self.modrm(d["num"], s)
        if d["kind"] == "mem" and s["kind"] in ("r8", "r16"):
            if s["num"] == 0 and self.direct(d):
                v, _ = self.value(d["disp"])
                return self.seg_prefix(d) + bytes([0xA2 + w, v & 0xFF,
                                                   (v >> 8) & 0xFF])
            return self.seg_prefix(d) + bytes([0x88 + w]) + self.modrm(s["num"], d)
        if d["kind"] in ("r8", "r16") and s["kind"] in ("r8", "r16"):
            if d["kind"] != s["kind"]:
                raise AsmError("operand sizes do not match")
            return bytes([0x8A + w]) + self.modrm(d["num"], s)
        raise AsmError("unsupported operands for MOV")

    def alu(self, n, ops, st):
        d, s = ops
        size = self.op_size(d, s)
        if size is None:
            raise AsmError("operand size is ambiguous (use BYTE PTR or WORD PTR)")
        w = 1 if size == "w" else 0
        pfx = self.seg_prefix(d, s)
        if s["kind"] == "imm":
            v = s["val"]
            wide = not s["known"] or getattr(st, "wide", False)
            if not s["known"]:
                st.wide = True
            small = (not wide) and self.fits8(v)
            if d["kind"] in ("r8", "r16") and d["num"] == 0:
                if size == "b":
                    return bytes([n * 8 + 4]) + self.imm_bytes(v, "b")
                return bytes([n * 8 + 5]) + self.imm_bytes(v, "w")
            if size == "b":
                return pfx + b"\x80" + self.modrm(n, d) + self.imm_bytes(v, "b")
            if small:
                return pfx + b"\x83" + self.modrm(n, d) + bytes([v & 0xFF])
            return pfx + b"\x81" + self.modrm(n, d) + self.imm_bytes(v, "w")
        if s["kind"] in ("r8", "r16"):
            if d["kind"] in ("r8", "r16"):
                return bytes([n * 8 + 2 + w]) + self.modrm(d["num"], s)
            return pfx + bytes([n * 8 + w]) + self.modrm(s["num"], d)
        if s["kind"] == "mem" and d["kind"] in ("r8", "r16"):
            return pfx + bytes([n * 8 + 2 + w]) + self.modrm(d["num"], s)
        raise AsmError("unsupported operands")

    def test(self, ops, st):
        d, s = ops
        size = self.op_size(d, s)
        if size is None:
            raise AsmError("operand size is ambiguous (use BYTE PTR or WORD PTR)")
        w = 1 if size == "w" else 0
        pfx = self.seg_prefix(d, s)
        if s["kind"] == "imm":
            if d["kind"] in ("r8", "r16") and d["num"] == 0:
                return bytes([0xA8 + w]) + self.imm_bytes(s["val"], size)
            return pfx + bytes([0xF6 + w]) + self.modrm(0, d) \
                + self.imm_bytes(s["val"], size)
        if s["kind"] in ("r8", "r16"):
            return pfx + bytes([0x84 + w]) + self.modrm(s["num"], d)
        return pfx + bytes([0x84 + w]) + self.modrm(d["num"], s)

    def incdec(self, dec, o):
        n = 1 if dec else 0
        if o["kind"] == "r16":
            return bytes([(0x48 if dec else 0x40) + o["num"]])
        size = self.op_size(o)
        if size is None:
            raise AsmError("operand size is ambiguous")
        return self.seg_prefix(o) + bytes([0xFE + (1 if size == "w" else 0)]) \
            + self.modrm(n, o)

    def unary(self, n, o):
        size = self.op_size(o)
        if size is None:
            raise AsmError("operand size is ambiguous")
        return self.seg_prefix(o) + bytes([0xF6 + (1 if size == "w" else 0)]) \
            + self.modrm(n, o)

    def shift(self, n, ops, st):
        d, c = ops
        size = self.op_size(d)
        if size is None:
            raise AsmError("operand size is ambiguous")
        w = 1 if size == "w" else 0
        pfx = self.seg_prefix(d)
        if c["kind"] == "r8" and c["name"] == "cl":
            return pfx + bytes([0xD2 + w]) + self.modrm(n, d)
        if c["kind"] == "imm" and c["val"] == 1:
            return pfx + bytes([0xD0 + w]) + self.modrm(n, d)
        raise AsmError("8086 shifts take a count of 1 or CL")

    def push(self, o):
        if o["kind"] == "r16":
            return bytes([0x50 + o["num"]])
        if o["kind"] == "sr":
            return bytes([0x06 | (o["num"] << 3)])
        if o["kind"] == "mem":
            return self.seg_prefix(o) + b"\xFF" + self.modrm(6, o)
        raise AsmError("the 8086 cannot push an immediate")

    def pop(self, o):
        if o["kind"] == "r16":
            return bytes([0x58 + o["num"]])
        if o["kind"] == "sr":
            if o["num"] == 1:
                raise AsmError("POP CS is not allowed")
            return bytes([0x07 | (o["num"] << 3)])
        if o["kind"] == "mem":
            return self.seg_prefix(o) + b"\x8F" + self.modrm(0, o)
        raise AsmError("bad POP operand")

    def inout(self, m, ops):
        if m == "out":
            port, reg = ops
        else:
            reg, port = ops
        w = 1 if reg["kind"] == "r16" else 0
        if port["kind"] == "r16" and port["name"] == "dx":
            return bytes([(0xEC if m == "in" else 0xEE) + w])
        if port["kind"] == "imm":
            return bytes([(0xE4 if m == "in" else 0xE6) + w, port["val"] & 0xFF])
        raise AsmError("port must be DX or an immediate")

    def xchg(self, ops):
        d, s = ops
        if d["kind"] == "r16" and s["kind"] == "r16":
            if d["num"] == 0:
                return bytes([0x90 + s["num"]])
            if s["num"] == 0:
                return bytes([0x90 + d["num"]])
        size = self.op_size(d, s)
        w = 1 if size == "w" else 0
        if s["kind"] in ("r8", "r16"):
            return self.seg_prefix(d) + bytes([0x86 + w]) + self.modrm(s["num"], d)
        return self.seg_prefix(s) + bytes([0x86 + w]) + self.modrm(d["num"], s)

    # ---- data ----
    def data_bytes(self, st):
        size = st.size
        out = bytearray()

        def emit(items):
            for item in items:
                if item[0] == "str":
                    for b in item[1]:
                        out.extend(bytes([b]) if size == "db" else
                                   bytes([b, 0]) if size == "dw" else
                                   bytes([b, 0, 0, 0]))
                elif item[0] == "undef":
                    out.extend(bytes({"db": 1, "dw": 2, "dd": 4}[size]))
                elif item[0] == "dup":
                    count, known = self.value(item[1])
                    if not known:
                        raise AsmError("DUP count must be defined before use")
                    inner = Statement("data", st.where, name=None, size=size,
                                      items=item[2])
                    chunk = self.data_bytes(inner)
                    out.extend(chunk * count)
                else:
                    v, known = self.value(item[1])
                    if size == "db":
                        if not -128 <= v <= 255:
                            raise AsmError(f"byte value {v} out of range")
                        out.append(v & 0xFF)
                    elif size == "dw":
                        if not -32768 <= v <= 65535:
                            raise AsmError(f"word value {v} out of range")
                        out.extend((v & 0xFFFF).to_bytes(2, "little"))
                    else:
                        out.extend((v & 0xFFFFFFFF).to_bytes(4, "little"))

        emit(st.items)
        return bytes(out)

    # ---- passes ----
    def run(self):
        self.final = False               # pass 1: sizes and addresses
        self.values = {}
        self.layout(first=True)
        before = dict(self.values)
        self.final = True                # pass 2: emit, with every value known
        out = self.layout(first=False)
        if self.values != before:
            changed = sorted(k for k in self.values
                             if self.values[k] != before.get(k))
            raise AsmError("phase error between passes: " + ", ".join(changed[:5]))
        return out

    def layout(self, first):
        out = bytearray()
        self.pc = 0
        origin = None
        new_values = {}
        for st in self.stmts:
            try:
                if st.kind == "label":
                    new_values[st.name] = self.values[st.name] = self.pc
                elif st.kind == "equ":
                    v, _ = self.value(st.ast)
                    new_values[st.name] = self.values[st.name] = v
                elif st.kind == "org":
                    v = self.eval(st.ast)
                    if origin is None:
                        origin = v
                        self.pc = v
                    elif v >= self.pc:
                        out.extend(bytes(v - self.pc))
                        self.pc = v
                    else:
                        raise AsmError("ORG moves backwards")
                elif st.kind == "data":
                    if st.name:
                        new_values[st.name] = self.values[st.name] = self.pc
                    if origin is None:
                        origin = self.pc
                    data = self.data_bytes(st)
                    out.extend(data)
                    self.pc += len(data)
                elif st.kind == "instr":
                    if origin is None:
                        origin = self.pc
                    code = self.encode(st, self.pc, self.final)
                    if first:
                        st.size_hint = len(code)
                    elif len(code) != st.size_hint:
                        raise AsmError(f"phase error ({st.size_hint} -> "
                                       f"{len(code)} bytes)")
                    out.extend(code)
                    self.pc += len(code)
            except Unresolved as e:
                raise AsmError(f"{st.where}: {e} is used before it is defined") \
                    from None
            except AsmError as e:
                if str(e).startswith(st.where):
                    raise
                raise AsmError(f"{st.where}: {e}") from None
        self.values = new_values
        self.origin = origin or 0
        return bytes(out)


def assemble_file(path):
    lines = expand_macros(load_lines(path))
    stmts = parse_statements(lines)
    asm = Assembler(stmts)
    data = asm.run()
    return data, asm.origin, asm.values


def main(argv=None):
    ap = argparse.ArgumentParser(description="WinKit 8086 assembler")
    ap.add_argument("source")
    ap.add_argument("-o", "--output", help="flat binary to write")
    ap.add_argument("-m", "--map", action="store_true",
                    help="print the symbol table")
    args = ap.parse_args(argv)
    try:
        data, origin, symbols = assemble_file(args.source)
    except AsmError as e:
        print(f"wkasm: {e}", file=sys.stderr)
        return 1
    if args.output:
        with open(args.output, "wb") as fh:
            fh.write(data)
    print(f"wkasm: {args.source}: {len(data)} bytes, origin {origin:#06x}")
    if args.map:
        for name, value in sorted(symbols.items(), key=lambda kv: kv[1]):
            if not name.startswith("??"):
                print(f"  {value & 0xFFFF:04X}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
