#!/usr/bin/env python3
"""Drives the interactive editor through a real pseudo-terminal. SPDX-License-Identifier: MIT"""
import fcntl, os, pty, select, signal, struct, sys, tempfile, termios, time

H = os.environ.get("HED") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hed")
H = os.path.abspath(H)
C = lambda c: chr(ord(c) & 0x1F)

def run(args, keys, stdin_data=None, resize_after=None):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        if stdin_data is not None:
            r, w = os.pipe(); os.write(w, stdin_data.encode()); os.close(w); os.dup2(r, 0)
            o = os.open("pipe_out.txt", os.O_WRONLY | os.O_CREAT | os.O_TRUNC); os.dup2(o, 1)
        os.execv(H, [H] + args)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    def drain(t):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try: os.read(fd, 65536)
                except OSError: return
    drain(0.5)
    for i, k in enumerate(keys):
        if resize_after is not None and i == resize_after:
            fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 40, 0, 0))
            os.kill(pid, signal.SIGWINCH); drain(0.2)
        os.write(fd, k.encode()); drain(0.15)
    drain(0.4)
    _, st = os.waitpid(pid, 0)
    return os.waitstatus_to_exitcode(st)

def put(name, s): open(name, "w").write(s)
def get(name): return open(name).read()

results = []
def check(name, cond):
    results.append(cond)
    print(("ok   " if cond else "FAIL ") + name)

os.chdir(tempfile.mkdtemp())
run(["new.py"], ["def f(x):\r", "return x\r", C("s"), C("x")])
check("auto-indent + save", get("new.py").startswith("def f(x):\n    return x\n"))

put("s.txt", "alpha beta\ngamma beta\n")
run(["s.txt"], [C("r"), "beta\r", "BETA\r", "a", C("z"), C("w"), "gamma\r", C("k"), C("s"), C("x")])
check("replace-all, undo, ^W find, ^K cut", get("s.txt") == "alpha beta\n")

put("f.txt", "one\ntwo\nthree\n")
run(["f.txt"], [C("f"), "thr\r", "X", C("s"), C("x")])
check("^F search moves cursor", get("f.txt") == "one\ntwo\nXthree\n")

put("n.txt", "abc abc abc\n")
run(["n.txt"], [C("w"), "abc\r", C("n"), C("n"), "Z", C("s"), C("x")])
check("^N find next", get("n.txt") == "abc abc Zabc\n")

run([], [C("e"), " world", C("x")], stdin_data="hello\n")
check("pipe mode ^X emits buffer", get("pipe_out.txt") == "hello world\n")
rc = run([], ["zzz", C("q")], stdin_data="hello\n")
check("pipe mode ^Q aborts", rc == 1 and get("pipe_out.txt") == "")

put("p.js", "")
run(["p.js"], ["\x1b[200~const a = 1;\nconst b = 2;\n\x1b[201~", C("_"), "1\r", "\x1b3", C("s"), C("x")])
check("bracketed paste + goto + comment", get("p.js").startswith("// const a = 1;\nconst b = 2;\n"))

put("sp.md", "This is teh test.\n")
run(["sp.md"], [C("t"), "1", C("s"), C("x")])
check("spell walk fixes word", get("sp.md") == "This is the test.\n")

run(["sp.md"], ["XYZ", C("x"), "n"])
check("dirty exit, answer no", get("sp.md") == "This is the test.\n")

put("u.txt", "héllo 日本 x\n")
run(["u.txt"], [C("e"), "\x1b[D", "\x1b[D", "!", C("s"), C("x")])
check("UTF-8 / wide-char cursor movement", get("u.txt") == "héllo 日本! x\n")

put("w.txt", "日本語\n")
run(["w.txt"], [C("e"), "\x1b[D", "\x1b[D", "|", C("s"), C("x")])
check("left over wide chars", get("w.txt") == "日|本語\n")

put("r.txt", "a\n")
rc = run(["r.txt"], [C("e"), "b", C("s"), C("x")], resize_after=1)
check("survives SIGWINCH resize", rc == 0 and get("r.txt") == "ab\n")

print(f"passed: {sum(results)}  failed: {len(results) - sum(results)}")
sys.exit(0 if all(results) else 1)
