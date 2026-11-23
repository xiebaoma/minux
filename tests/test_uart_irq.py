"""Verify kernel selftests, the /init process lifecycle and UART reception in QEMU."""

import os
import re
import select
import subprocess
import sys
import time


FIRST = b"IRQ-one"
SECOND = b"IRQ-two"
SECOND_ECHO = SECOND + b"\r\n"
TIMEOUT = 20


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: test_uart_irq.py QEMU [QEMU arguments...]")

    output = bytearray()
    deadline = time.monotonic() + TIMEOUT
    proc = subprocess.Popen(
        sys.argv[1:], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
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

    try:
        # stdin is a pipe, so any echoed bytes must come from the guest UART.
        # Wait for complete log lines so the echo cannot interrupt their output.
        ready = wait_for(b"\r\n", wait_for(b"timer: 10 Hz, timebase: "))
        # /init may resume after the timer banner. Let its exit log finish before
        # injecting characters so kernel output cannot split the UART echo.
        init = re.search(rb"user task (\d+): start /init\r\n", output)
        if not init:
            raise RuntimeError("missing /init launch")
        child_marker = b": exec /bin/child\r\n"
        wait_for(child_marker, init.end())
        child = re.compile(rb"user task (\d+): exec /bin/child\r\n").search(output, init.end())
        if not child or child[1] == init[1]:
            raise RuntimeError("/init did not exec a separate child")
        child_done = wait_for(b"user task " + child[1] + b": exit 42\r\n", child.end())
        init_done = wait_for(b"user task " + init[1] + b": exit 0\r\n", child_done)
        ready = max(ready, init_done)
        proc.stdin.write(FIRST)
        proc.stdin.flush()
        first_end = wait_for(FIRST, ready)

        # A timer interrupt between inputs proves the first IRQ handler returned.
        tick_end = wait_for(b"timer: 10 ticks\r\n", first_end)
        proc.stdin.write(SECOND + b"\r")
        proc.stdin.flush()
        wait_for(SECOND_ECHO, tick_end)

        while read_chunk():
            pass
        proc.wait(timeout=max(0.1, deadline - time.monotonic()))

        summary = re.search(rb"==== (\d+)/(\d+) passed, all OK ====", output)
        if not summary or summary.group(1) != summary.group(2):
            raise RuntimeError("kernel selftests did not all pass")
        if output.count(FIRST) != 1 or output.count(SECOND_ECHO) != 1:
            raise RuntimeError("UART input was not echoed exactly once")
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

    print("/init clone/COW/execve/exit/wait4 integration test passed")
    print("UART/PLIC integration test passed")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"UART/PLIC integration test failed: {exc}") from exc
