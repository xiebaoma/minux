"""Build a fixed-geometry ext2 image using host e2fsprogs; never mount it."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


UUID = "6d696e75-7800-4000-8000-000000000007"
STAMP = "1700000000"


def tool(name):
    path = shutil.which(name)
    if path:
        return path
    for directory in ("/usr/sbin", "/opt/homebrew/opt/e2fsprogs/sbin",
                      "/usr/local/opt/e2fsprogs/sbin"):
        path = Path(directory, name)
        if path.is_file():
            return str(path)
    raise SystemExit(f"{name} is required; install e2fsprogs")


def build(output, child, tail):
    env = dict(os.environ, E2FSPROGS_FAKE_TIME=STAMP, LC_ALL="C", TZ="UTC")
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        work = Path(tmp)
        image = work / "disk.img"
        subprocess.run([tool("mke2fs"), "-q", "-F", "-t", "ext2", "-b", "1024",
                        "-g", "8192", "-r", "1", "-o", "linux",
                        "-I", "256", "-N", "128", "-m", "0", "-O", "none,filetype",
                        "-U", UUID, "-E", f"hash_seed={UUID},lazy_itable_init=0",
                        str(image), "2048"], env=env, check=True)
        shutil.copyfile(child, work / "child")
        (work / "pattern").write_bytes(bytes((i * 17 + i // 1024) & 255
                                             for i in range(17 * 1024 + 37)))
        commands = ["mkdir bin", "mkdir share", "mkdir share/probe",
                    "write child bin/child", "write pattern share/probe/pattern",
                    "set_inode_field bin/child mode 0100755",
                    "set_inode_field share/probe/pattern mode 0100644"]
        for name in ("bin/child", "share/probe/pattern"):
            for field in ("uid", "gid"):
                commands.append(f"set_inode_field {name} {field} 0")
            for field in ("ctime", "atime", "mtime"):
                commands.append(f"set_inode_field {name} {field} @{STAMP}")
        commands.extend(["dump bin/child check-child", "dump share/probe/pattern check-pattern"])
        subprocess.run([tool("debugfs"), "-w", "-f", "-", str(image)], cwd=work,
                       input="\n".join(commands) + "\n", text=True, env=env, check=True)
        for name in ("child", "pattern"):
            if (work / f"check-{name}").read_bytes() != (work / name).read_bytes():
                raise SystemExit(f"ext2 payload mismatch: {name}")
        subprocess.run([tool("e2fsck"), "-fn", str(image)], env=env, check=True)
        if tail:
            with image.open("ab") as f:
                f.write(tail.read_bytes())
        image.replace(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("child", type=Path)
    parser.add_argument("--sector-fixture", type=Path)
    args = parser.parse_args()
    build(args.output.resolve(), args.child, args.sector_fixture)
