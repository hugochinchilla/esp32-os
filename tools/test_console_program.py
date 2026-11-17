#!/usr/bin/env python3
"""Runs the command shell's PC program (chinchilla-console) with lines piped
in, once on its RAM disk and once on a real folder, and checks what it prints
and what it leaves in the folder.

    test_console_program.py build/host/chinchilla-console
"""
import os
import subprocess
import sys
import tempfile


def run(program, lines, *args):
    text = "".join(line + "\n" for line in lines)
    done = subprocess.run([program, *args], input=text.encode("utf-8"), stdout=subprocess.PIPE, timeout=30)
    assert done.returncode == 0, done.returncode
    return done.stdout.decode("utf-8").replace("\r\n", "\n")


def typed_at_a_terminal(program):
    """The program on a terminal (a pty), keys sent one group at a time as a
    person types: each is answered before the next is sent."""
    import pty
    import select
    import time

    pid, fd = pty.fork()
    if pid == 0:
        os.execv(program, [program])
    seen = b""

    def until(text, seconds=10):
        nonlocal seen
        deadline = time.time() + seconds
        while text not in seen:
            left = deadline - time.time()
            assert left > 0, (text, seen)
            if select.select([fd], [], [], left)[0]:
                seen += os.read(fd, 4096)
        at = seen.index(text) + len(text)
        before, seen = seen[:at], seen[at:]
        return before

    until(b"C:\\> ")
    # A typing mistake put right with the left arrow and backspace.
    os.write(fd, b"echo helxlo")
    until(b"helxlo")
    os.write(fd, b"\x1b[D\x1b[D\x7f\r")
    until(b"\r\nhello\r\nC:\\> ")
    # Ctrl+C is Chinchilla's, not the PC's: the program is still there.
    os.write(fd, b"half a line\x03")
    until(b"half a line^C\r\nC:\\> ")
    # The line before last, with the up arrow.
    os.write(fd, b"\x1b[A\r")
    until(b"hello\r\nC:\\> ")
    os.write(fd, "cd Documents\rtype espa\u00f1ol.txt\r".encode("utf-8"))
    until("Texto en espa\u00f1ol".encode("utf-8"))
    os.write(fd, b"\x04")  # Ctrl+D
    _, status = os.waitpid(pid, 0)
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, status
    os.close(fd)


def main():
    program = sys.argv[1]

    # The RAM disk with the sample files. The lines are all typed before the
    # first prompt, so their echo comes first and the answers after.
    out = run(program, ["cd Documents", "type español.txt", "ps", "kill 1", "nonsense"])
    assert "help lists the commands." in out, out
    assert "C:\\Documents> Texto en español: año, niño, café, ¡hola!" in out, out
    assert "    1     0  SP--     0.0s      -  desktop" in out, out
    assert "kill: 1: it is protected" in out, out
    assert "nonsense: there is no such command" in out, out

    # A folder of the PC as C:.
    with tempfile.TemporaryDirectory() as folder:
        with open(os.path.join(folder, "hello.txt"), "wb") as f:
            f.write("año\n".encode("latin-1"))
        out = run(program, ["mkdir Cartas", "copy hello.txt Cartas\\adiós.txt", "ren hello.txt Hola.txt", "dir",
                            "type Cartas/adiós.txt", "exit", "echo not reached"], folder)
        assert " <DIR>           Cartas" in out, out
        assert "              4  Hola.txt" in out, out
        assert "C:\\> año" in out, out
        assert "not reached\n" not in out.split("C:\\> año")[1], out
        assert sorted(os.listdir(folder)) == ["Cartas", "Hola.txt"], os.listdir(folder)
        with open(os.path.join(folder, "Cartas", "adiós.txt"), "rb") as f:
            assert f.read() == "año\n".encode("latin-1")

    typed_at_a_terminal(program)
    print("console program: all checks passed")


if __name__ == "__main__":
    main()
