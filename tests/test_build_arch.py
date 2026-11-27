"""Exercise architecture selection without compiling a second set of images."""

import os
import subprocess
import unittest


def plan(*options, check=True):
    # Exercise the defaults even when the parent was invoked with ARCH or TEST.
    env = os.environ.copy()
    for name in ("ARCH", "TEST", "MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEOVERRIDES"):
        env.pop(name, None)
    return subprocess.run(
        [env.get("MAKE", "make"), "-Bn", "--no-print-directory", *options, "run"],
        env=env, capture_output=True, text=True, check=check,
    )


class BuildSelectionTest(unittest.TestCase):
    def test_default_matches_riscv(self):
        default = plan().stdout
        self.assertEqual(default, plan("ARCH=riscv").stdout)
        self.assertIn("-kernel build/minux.elf", default)
        self.assertNotIn("-DCONFIG_SELFTEST", default)

    def test_arch_suites_and_probes_only_join_selftest_kernel(self):
        release = plan("ARCH=riscv").stdout
        selftest = plan("ARCH=riscv", "TEST=1").stdout
        self.assertIn("-kernel build/test/minux.elf", selftest)
        self.assertIn("-DCONFIG_SELFTEST", selftest)
        for name in ("test_sv39", "sv39_probe", "user_probe", "user_fault_probe", "user_wait_probe"):
            obj = f"arch/riscv/tests/{name}.o"
            self.assertNotIn(obj, release)
            self.assertIn(f"build/test/{obj}", selftest)

    def test_unknown_arch_is_rejected(self):
        result = plan("ARCH=missing-arch", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsupported ARCH 'missing-arch'", result.stderr)


if __name__ == "__main__":
    unittest.main()
