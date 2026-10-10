"""Write harness.c: the C part from harness.in.c plus generated assembly.

The harness stands in for BrgGame-Steam.exe. For every floor event the plugin
fixes it has a little handler shaped like the game's: it logs its message to
the normal log, then to the fatal one, and returns false. The messages are
read from the plugin's own list, so the two cannot drift apart. Like the game:

  - two messages (the zombie reward pair) share one fatal call, reached by a
    long and a short jump
  - one message (the mboss kill) has two handlers, so two fatal calls
  - three inventory handlers (beast, material, vending machine) log their
    "not found" the same way and must stay fatal

Usage: python tests/make_harness.py OUTPUT.c [--drop N]
  --drop N leaves out event N's fatal call, as a game update might.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "dll" / "lid_offlinefixes.c"

table = SOURCE.read_text(encoding="utf-8").split("FLOOR_EVENTS[] = {", 1)[1].split("};", 1)[0]
EVENTS = re.findall(r'L"((?:[^"\\]|\\.)*)"', table)
KEEP = ["current user get beast entity not found beast_entity_id=%s",
        "user get material entity not found item_entity_id=%s",
        "current user get vending machine entity not found vending_machine_entity_id=%s"]
SHARED = (4, 5)        # user_get_zombie_reward zombie / zombie_reward
TWICE = 13             # not found kill mboss entity

out_path = Path(sys.argv[1])
drop = int(sys.argv[sys.argv.index("--drop") + 1]) if "--drop" in sys.argv else -1


def asm(lines):
    return ''.join('    "%s\\n"\n' % l.replace('\\', '\\\\').replace('"', '\\"') for l in lines)


def b(hexes):
    return '.byte ' + ','.join('0x' + h for h in hexes.split())


def wide(label, text):
    return [f'{label}:', '.short ' + ','.join(str(ord(c)) for c in text) + ',0']


def lea_rdx(label):
    return [b('48 8D 15'), f'.long {label} - (. + 4)']


def log_to(device):            # mov rcx,[rip+device] / call fake_logf
    return [b('48 8B 0D'), f'.long {device} - (. + 4)', 'call fake_logf']


ENTER = ['pushq %rbx', 'subq $0x20, %rsp', 'movb $1, %bl']
LEAVE = ['movzbl %bl, %eax', 'addq $0x20, %rsp', 'popq %rbx', 'ret']

strings = ['.section .rdata,"dr"', '.p2align 1']
code = ['.text']
handlers = []
messages = [(f"ev{i}", t, i != drop) for i, t in enumerate(EVENTS)] + \
           [(f"keep{i}", t, True) for i, t in enumerate(KEEP)]

for name, text, fatal in messages:
    strings += wide(f"{name}_normal", text) + wide(f"{name}_fatal", text)

for name, text, fatal in messages:
    index = int(name[2:]) if name.startswith("ev") else -1
    if index in SHARED:
        continue
    copies = 2 if index == TWICE else 1
    for n in range(copies):
        h = f"h_{name}_{n}"
        handlers.append(h)
        code += [f'.globl {h}', f'{h}:', *ENTER, *lea_rdx(f"{name}_normal"), *log_to("g_normal")]
        if fatal:
            code += [*lea_rdx(f"{name}_fatal"), b('4C 8B C3'), *log_to("g_fatal")]
        code += [b('32 DB'), *LEAVE]     # "not found": answer no

# The shared pair: the first jumps far to the tail, the second falls into it
# through a short jump; the tail moves two arguments before the call.
first, second = (f"ev{i}" for i in SHARED)
code += ['.globl h_shared_0', 'h_shared_0:', *ENTER, *lea_rdx(f"{first}_normal"), *log_to("g_normal"),
         *lea_rdx(f"{first}_fatal"), b('E9'), '.long shared_tail - (. + 4)',
         '.globl h_shared_1', 'h_shared_1:', *ENTER, *lea_rdx(f"{second}_normal"), *log_to("g_normal"),
         *lea_rdx(f"{second}_fatal"), b('EB 00'),
         'shared_tail:', b('4C 8B CF 4C 8B C3'), *log_to("g_fatal"), b('32 DB'), *LEAVE]
handlers += ['h_shared_0', 'h_shared_1']

code += ['.data', '.p2align 3', '.globl handlers', 'handlers:'] + [f'.quad {h}' for h in handlers]
code += ['.globl handler_count', 'handler_count:', f'.long {len(handlers)}']

body = (HERE / 'harness.in.c').read_text().replace('@ASM@', asm(strings + code))
body = body.replace('@KEEP@', str(len(KEEP))).replace('@DROP@', str(drop))
out_path.write_text(body)
