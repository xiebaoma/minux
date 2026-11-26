"""Verify kernel selftests, the /init process lifecycle and UART reception in QEMU."""

import os
import re
import select
import subprocess
import sys
import time


TYPED = b"IRQ-onxy"
EDITED_LINE = b"IRQ-one\r\n"
PROMPT = b"minux$ "
TIMEOUT = 20


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: test_uart_irq.py EXPECTED_STATUS QEMU [QEMU arguments...]")

    expected = str(int(sys.argv[1])).encode()
    output = bytearray()
    deadline = time.monotonic() + TIMEOUT
    proc = subprocess.Popen(
        sys.argv[2:], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    def read_chunk():
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([proc.stdout], [], [], remaining)[0]:
            raise RuntimeError("QEMU timed out")
        chunk = os.read(proc.stdout.fileno(), 4096)
        if chunk:
            output.extend(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
        return chunk

    def wait_for(marker, start=0):
        while True:
            pos = output.find(marker, start)
            if pos >= 0:
                return pos + len(marker)
            if not read_chunk():
                raise RuntimeError(f"QEMU exited before {marker!r}")

    def wait_for_child_exec(path, start, parent_pid):
        marker = b": exec " + path + b"\r\n"
        end = wait_for(marker, start)
        match = re.search(rb"user task (\d+)" + re.escape(marker), output[start:end])
        if not match or match[1] == parent_pid:
            raise RuntimeError(f"{path!r} did not run in a child")
        return match[1], end

    def send(keys):
        proc.stdin.write(keys)
        proc.stdin.flush()

    try:
        # stdin is a pipe, so any echoed bytes must come from the guest UART.
        # Wait for complete log lines so the echo cannot interrupt their output.
        ready = wait_for(b"\r\n", wait_for(b"timer: 10 Hz, timebase: "))
        init = re.search(rb"user task (\d+): start /init\r\n", output)
        if not init:
            raise RuntimeError("missing /init launch")
        hello_pid, hello_start = wait_for_child_exec(b"/bin/hello", init.end(), init[1])
        greeting = wait_for(b"hello from Minux libc\r\n", hello_start)
        hello_exit = wait_for(b"user task " + hello_pid + b": exit 37\r\n", greeting)
        hello_done = wait_for(b"init: reaped /bin/hello, status 37\r\n", hello_exit)
        disk_pid, disk_start = wait_for_child_exec(b"/disk/bin/child", hello_done, init[1])
        child_done = wait_for(b"user task " + disk_pid + b": exit " + expected + b"\r\n", disk_start)
        reaped = wait_for(b"init: reaped disk child, status " + expected + b"\r\n", child_done)
        console_done = wait_for(b"minux: hello through pipe\r\n", reaped)
        shell_start = wait_for(b"user task " + init[1] + b": exec /bin/sh\r\n", console_done)
        prompt = wait_for(PROMPT, shell_start)
        send(b"/bin/echo hello\n")
        echo_pid, echo_start = wait_for_child_exec(b"/bin/echo", max(ready, prompt), init[1])
        hello = wait_for(b"hello\r\n", echo_start)
        prompt = wait_for(PROMPT, wait_for(b"user task " + echo_pid + b": exit 0\r\n", hello))

        send(b"/bin/missing\n")
        failed = wait_for(b"sh: cannot execute /bin/missing\r\n", prompt)
        prompt = wait_for(PROMPT, wait_for(b": exit 127\r\n", failed))
        for command, error in [
            (b"echo hello", b"sh: expected an absolute path"),
            (b"/bin/echo " + b"x" * 118, b"sh: command line too long"),
            (b"/bin/echo" + b" x" * 32, b"sh: too many arguments"),
        ]:
            send(command + b"\n")
            prompt = wait_for(PROMPT, wait_for(error + b"\r\n", prompt))
        send(b"  \t\n")
        prompt = wait_for(PROMPT, prompt)
        send(b"  /bin/echo\tstill   here \n")
        echo_pid, echo_start = wait_for_child_exec(b"/bin/echo", prompt, init[1])
        recovered = wait_for(b"still here\r\n", echo_start)
        prompt = wait_for(PROMPT, wait_for(b"user task " + echo_pid + b": exit 0\r\n", recovered))

        send(b"/bin/cat\n")
        cat_pid, cat_start = wait_for_child_exec(b"/bin/cat", prompt, init[1])
        send(TYPED)
        first_end = wait_for(TYPED, cat_start)
        # The unfinished line is echoed immediately, while cat stays blocked.
        tick_end = wait_for(b"timer: 10 ticks\r\n", first_end)
        send(b"\x7f\be\r")
        echoed = wait_for(b"\b \b\b \be\r\n", tick_end)
        delivered = wait_for(EDITED_LINE, echoed)
        # Nonempty Ctrl-D submits bytes without a newline; the next read waits again.
        send(b"tail\x04")
        partial = wait_for(b"tailtail", delivered)
        send(b"IRQ-two\n")
        complete = wait_for(b"IRQ-two\r\nIRQ-two\r\n", partial)
        send(b"\x04")
        cat_exit = wait_for(b"user task " + cat_pid + b": exit 0\r\n", complete)
        if PROMPT in output[cat_start:cat_exit]:
            raise RuntimeError("shell prompted while cat still owned stdin")
        prompt = wait_for(PROMPT, cat_exit)
        send(b"\x04")
        wait_for(b"user task " + init[1] + b": exit 0\r\n", prompt)

        while read_chunk():
            pass
        proc.wait(timeout=max(0.1, deadline - time.monotonic()))

        summary = re.search(rb"==== (\d+)/(\d+) passed, all OK ====", output)
        if not summary or summary.group(1) != summary.group(2):
            raise RuntimeError("kernel selftests did not all pass")
        if output.count(TYPED) != 1 or output.count(EDITED_LINE) != 1:
            raise RuntimeError("unexpected TTY echo or edited line")
        if b"timer: 20 ticks" not in output:
            raise RuntimeError("timer did not continue after UART interrupts")
        if proc.returncode != 0:
            raise RuntimeError(f"QEMU exited with status {proc.returncode}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    print("Minux libc startup, argv/envp, write and main returning 37 passed")
    print("/tmp ramfs I/O, independent opens and shared fd across fork/exec passed")
    print(f"/init reaped /disk/bin/child with expected exit code {expected.decode()}")
    print("anonymous pipe, EOF and stdout via /dev/console passed")
    print("UART/PLIC, canonical stdin, echo, erase and Ctrl-D integration test passed")
    print("Shell foreground commands, argument parsing and recovery after errors passed")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"UART/PLIC integration test failed: {exc}") from exc
