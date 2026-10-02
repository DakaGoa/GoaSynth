#!/usr/bin/env python3
"""Build every test target, prove the binaries are current, then run them.

    python tools/run-checks.py                 # build, verify, run the lot
    python tools/run-checks.py -R DocsCheck    # one test, still built and verified
    python tools/run-checks.py --list          # what it would run, and costs
    python tools/run-checks.py --no-build      # verify and run what is there

Why this exists, rather than `ctest` on its own
----------------------------------------------
Two ways a test suite lies, both seen for real in this repository:

  * A **failed build leaves the previous binary in place.** cmake --build exits
    non-zero, the old executable is still on disk, and running it prints the old
    result - a stale `DOCS CHECK OK` from a build that never happened. A run that
    is silent because the process died before printing anything looks exactly
    like a pass, and on Windows/MSYS a fast-fail (0xC0000409) can even report
    exit code 0 depending on how it is launched.
  * A test binary can be **older than the source it is supposed to test** - an
    edit made after the last successful build, a build that stopped at the first
    error, a target that was never in the build command. ctest cannot tell: it
    runs whatever executable is at that path.

So this builds first and refuses to continue if the build failed, then compares
each test binary against the files it was actually compiled from and refuses to
run anything whose binary is behind its sources. Refusing loudly is the point:
a suite that cannot be trusted must not report green.

Where the dependencies come from
--------------------------------
Not from a list kept here - a list would drift, which is the problem being
solved. They are read out of the build itself:

  * the target list comes from CMake, via `ctest --show-only=json-v1`, so a new
    test is picked up by being a test (`add_executable` + `add_test`);
  * each binary's inputs come from MSBuild's own dependency logs
    (`build/<target>.dir/<config>/<target>.tlog/*.read.*.tlog`), which name every
    file the compiler and linker actually read - sources, headers, JUCE, objs;
  * entries outside this repository (the MSVC headers, Windows DLLs) are ignored:
    those are the toolchain's business, and MSBuild already tracks them.

With a generator that writes no tlogs (Ninja, Makefiles) there is nothing to
read, so the check falls back to every file under `tests/` plus CMakeLists.txt -
conservative, and it says so rather than pretending to be exact.

Exit codes
----------
0 nothing to report   1 the build failed   2 setup problem (not configured, no
ctest, nothing discovered)   3 a binary is behind its sources   4 tests failed
- distinct, because each one sends you somewhere different.  Some of these are
deliberately not ctest's own codes.

Plain Python 3, no third-party modules, nothing to install.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Nothing here may die because a console cannot print a byte. When stdout is a
# pipe on Windows it decodes in cp1252, whose undefined C1 slots leave some
# characters unencodable - print() then raises in the middle of streaming the
# build or test output, and the runner exits having said nothing, which is the
# exact silent failure this tool exists to prevent. Replace what cannot be
# printed instead; the report survives, garbled in one glyph at most.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(errors="replace")
    except (AttributeError, ValueError, OSError):
        pass  # a stream that cannot be reconfigured is one that already copes

EXIT_OK, EXIT_BUILD, EXIT_SETUP, EXIT_STALE, EXIT_TESTS = 0, 1, 2, 3, 4

# A compiler or linker diagnostic, as MSBuild spells it. Not a bare "error",
# because MSBuild's own summary line prints "0 Error(s)" on every clean build.
BUILD_ERROR_RE = re.compile(r"error\s+[A-Z]{1,4}\d+|fatal error|:\s*error\b", re.I)
BUILD_WARNING_RE = re.compile(r"warning\s+[A-Z]{1,4}\d+")

# Clocks are not a source of truth, and a file written in the same second as the
# binary is not drift. A human edit that lands after a build misses by far more
# than this; a timestamp granularity artefact misses by less.
MTIME_TOLERANCE_S = 1.0


def die(message: str, code: int = EXIT_SETUP) -> int:
    print(f"\nrun-checks: {message}", file=sys.stderr)
    return code


def stamp(mtime: float) -> str:
    return dt.datetime.fromtimestamp(mtime).strftime("%Y-%m-%d %H:%M:%S")


def run_stream(cmd: list[str], cwd: Path | None = None) -> tuple[int, list[str]]:
    """Run a command, showing its output as it arrives and keeping it too."""
    proc = subprocess.Popen(cmd, cwd=str(cwd) if cwd else None, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                            errors="replace", bufsize=1)
    lines: list[str] = []
    assert proc.stdout is not None
    for raw in proc.stdout:
        line = raw.rstrip("\r\n")
        lines.append(line)
        print(line, flush=True)
    proc.wait()
    return proc.returncode, lines


#==============================================================================
# what the tests are, according to CMake
#==============================================================================
@dataclass
class Test:
    name: str
    exe: Path | None = None
    args: list[str] = field(default_factory=list)


def ctest_files(build_dir: Path, config: str) -> dict[str, Path]:
    """Test name -> executable, out of the CTestTestfile CMake wrote.

    Needed because ctest's JSON only resolves a test's command *once the
    executable exists* - so on a tree that has never been built it reports the
    tests with no command at all, which is exactly the tree that most needs
    building. The testfile is written at configure time and names the path
    either way.
    """
    # re.M matters: this is matched line by line inside a generated file, and
    # without it `^` and `$` anchor to the whole text, so every line misses and
    # the lookup comes back empty - the failure mode this function exists to
    # survive, arriving through the door marked "fallback".
    call = re.compile(r'^\s*add_test\(\s*(.+?)\s*\)\s*$', re.M)
    token = re.compile(r'"([^"]*)"|(\S+)')
    subdir = re.compile(r'^\s*subdirs\(\s*"([^"]+)"', re.M)
    candidates: dict[str, list[Path]] = {}
    seen: set[Path] = set()
    todo = [build_dir]

    # Walked the way ctest walks it - the top testfile plus whatever it lists as
    # subdirectories - rather than by globbing for every testfile under the build
    # tree, which would also find scratch projects somebody configured there.
    while todo:
        folder = todo.pop()
        testfile = folder / "CTestTestfile.cmake"

        if testfile in seen or not testfile.is_file():
            continue

        seen.add(testfile)

        try:
            text = testfile.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue

        for match in call.finditer(text):
            # The line is `add_test(<name> <exe> [args...])`, quoted where the
            # path needs it - and some tests here do take arguments, so the
            # whole call is tokenised rather than matched by shape.
            words = [m.group(1) if m.group(1) is not None else m.group(2)
                     for m in token.finditer(match.group(1))]

            if len(words) < 2:
                continue

            name, path = words[0], words[1]

            # `add_test(NAME ... NOT_AVAILABLE)` is how the testfile spells a
            # test this configuration cannot run.
            if path == "NOT_AVAILABLE":
                continue

            candidates.setdefault(name, []).append(Path(path))

        todo.extend(folder / sub for sub in subdir.findall(text))

    # A multi-config generator writes one add_test per configuration, all with
    # the name, so the path worth reporting is the one under the configuration
    # being run - otherwise a missing Release binary is reported at its Debug
    # path, which is a wild goose chase. Single-config layouts have no such
    # segment, and the first entry is then the only one there is.
    out: dict[str, Path] = {}

    for name, paths in candidates.items():
        wanted = next((p for p in paths
                       if any(part.lower() == config.lower() for part in p.parts)), None)
        out[name] = wanted or paths[0]

    return out


def discover(build_dir: Path, config: str) -> list[Test]:
    cmd = ["ctest", "--test-dir", str(build_dir), "-C", config, "--show-only=json-v1"]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                              errors="replace")
    except FileNotFoundError:
        raise SystemExit(die("ctest is not on PATH - install CMake, or add it to PATH"))

    if proc.returncode != 0:
        raise SystemExit(die(f"ctest could not list the tests:\n\n{proc.stdout}{proc.stderr}"))

    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise SystemExit(die(f"ctest's test list is not JSON ({exc})"))

    known = ctest_files(build_dir, config)
    tests: list[Test] = []

    for entry in data.get("tests", []):
        name = entry.get("name")
        command = entry.get("command") or []
        args: list[str] = []
        exe: Path | None = None

        if command:
            exe = Path(command[0])
            args = list(command[1:])
            if not exe.is_absolute():
                # A generator that does not resolve the target to a full path.
                # The usual layout still puts it under <build>/<config>/.
                exe = build_dir / config / exe.name
        else:
            # Not built yet: the testfile still says where it will be written.
            exe = known.get(name) if name else None

        if not name and exe:
            name = exe.stem

        if not name:
            continue

        tests.append(Test(name=name, exe=exe, args=args))

    if not tests:
        # A tree that is configured but has never been built does list its tests
        # here, just without commands. So an empty list means the configuration
        # is not one this tree was generated for, or it was never configured.
        names = sorted(ctest_files(build_dir, config))
        known = (f" It defines {len(names)}: {', '.join(names)}." if names else "")
        raise SystemExit(die(f"no tests found in {build_dir} for configuration "
                             f"{config!r}.{known}\n"
                             f"  Check --config, and that the tree was configured here."))

    return tests


def targets_for(tests: list[Test]) -> list[str]:
    """The build targets behind the tests, in the order CMake lists them.

    A test's target is the executable it runs, so the stem is the target name.
    Before the first build there is no executable to take it from, so the test's
    own name is used - and in this repository a test is named after the target
    it runs. The list is built rather than hard-coded: rename a target and this
    follows, add a test and it is included.
    """
    names: list[str] = []

    for test in tests:
        target = test.exe.stem if test.exe else test.name
        if target not in names:
            names.append(target)

    return names


#==============================================================================
# what each binary was actually built from
#==============================================================================
def read_tlog(path: Path) -> list[Path]:
    """The file list out of one MSBuild dependency log.

    The format is UTF-16LE with a byte order mark; entries are separated by
    CRLF, the first is prefixed with a caret, and paths may be quoted. It is
    undocumented and internal - which is why anything unreadable is treated as
    "no information" rather than as an error.
    """
    try:
        text = path.read_bytes().decode("utf-16-le", errors="replace")
    except OSError:
        return []

    text = text.lstrip("\ufeff")
    out: list[Path] = []

    for line in re.split(r"[\r\n]+", text):
        entry = line.strip().lstrip("^").strip().strip('"')
        if entry:
            out.append(Path(entry))

    return out


def source_root(build_dir: Path) -> Path:
    """Where the sources this build compiled live, asked of the build itself.

    CMakeCache.txt records the directory the tree was configured from, so a
    checks runner pointed at somebody else's build directory still compares the
    right files - and this file does not have to assume it is the tree it sits
    in.
    """
    try:
        cache = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ROOT

    match = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$", cache, re.M)
    return Path(match.group(1).strip()) if match else ROOT


def fallback_deps(root: Path) -> list[Path]:
    """What to compare against when the generator writes no dependency logs.

    Everything under tests/ plus CMakeLists.txt: the files a stale binary is
    most likely to be behind. Not the whole tree, because a change in Source/
    that a given test does not compile is not drift for that test - treating it
    as drift would refuse runs that are perfectly current.
    """
    out: list[Path] = []

    for base in (root / "tests",):
        if base.is_dir():
            out.extend(p for p in base.rglob("*") if p.is_file())

    out.extend(p for p in (root / "CMakeLists.txt",) if p.is_file())
    out.extend(p for p in (root / "cmake").glob("*.cmake"))

    return out


def under(path: Path, root: Path) -> Path | None:
    """`path` relative to `root`, or None if it is not inside it.

    Case-insensitively, because MSBuild writes its dependency logs in upper
    case on Windows and filesystems there do not distinguish case - a plain
    `path.relative_to(root)` would reject every entry and quietly leave the
    check with no inputs to compare at all. The returned tail is then joined
    back onto the root, so the paths that come out are spelled the way the tree
    is spelled rather than the way the log is.
    """
    try:
        return path.relative_to(root)
    except ValueError:
        pass

    head, tail = root.parts, path.parts

    if len(tail) <= len(head):
        return None

    if all(os.path.normcase(a) == os.path.normcase(b) for a, b in zip(head, tail)):
        return Path(*tail[len(head):])

    return None


def true_case(path: Path) -> Path:
    """The path spelled the way the filesystem spells it, for printing only.

    MSBuild's logs are upper case throughout, so echoing them back produces
    messages like `TESTS\\DOCSCHECK.CPP` - the right file, shouted. Where the
    filesystem does not distinguish case anyway, the real spelling can be looked
    up and used instead.
    """
    if os.name != "nt":
        return path

    parts = path.parts
    if not parts:
        return path

    out = Path(parts[0])

    for part in parts[1:]:
        try:
            match = next((c.name for c in out.iterdir() if c.name.lower() == part.lower()), None)
        except OSError:
            match = None
        out = out / (match if match else part)

    return out


def rel(path: Path, roots: tuple[Path, ...]) -> str:
    """A readable path for a message, whether or not it is inside the tree."""
    for root in roots:
        tail = under(true_case(path), root)
        if tail is not None:
            return str(tail)

    return str(path)


@dataclass
class Inputs:
    """What a binary was built from, as far as this machine can say."""

    newest: Path
    mtime: float
    count: int
    skipped: int      # inputs outside the tree: toolchain files, not the project's
    approximate: bool  # read from the fallback rather than a real dependency log


#==============================================================================
def newest_input(binary: Path, tlog_dir: Path, roots: tuple[Path, ...]) -> Inputs | None:
    """The newest file `binary` was built from, or None if nothing could be read."""
    deps: list[Path] = []
    skipped = 0

    if tlog_dir.is_dir():
        for tlog in sorted(tlog_dir.glob("*.read.*.tlog")):
            for path in read_tlog(tlog):
                # Sources, headers, objs and libs are all inside the source or
                # build tree. The MSVC headers and Windows DLLs that also appear
                # here are the toolchain's, not the project's.
                found = None

                for root in roots:
                    found = under(path, root)
                    if found is not None:
                        candidate = root / found
                        if candidate.is_file():
                            deps.append(candidate)
                        break

                if found is None:
                    skipped += 1

    approximate = not deps

    if approximate:
        deps = fallback_deps(roots[0])

    if not deps:
        return None

    newest = max(deps, key=lambda p: p.stat().st_mtime)
    return Inputs(newest=newest, mtime=newest.stat().st_mtime, count=len(deps),
                  skipped=skipped, approximate=approximate)


#==============================================================================
def main() -> int:
    parser = argparse.ArgumentParser(
        prog="run-checks.py",
        description="Build every test target, prove the binaries are current, run them.")
    parser.add_argument("-B", "--build-dir", default="build",
                        help="build directory, relative to the repository root (default: build)")
    parser.add_argument("--config", default="Release", help="configuration (default: Release)")
    parser.add_argument("-j", "--parallel", type=int, default=None,
                        help="build with N parallel jobs (default: the generator's own)")
    parser.add_argument("-R", "--tests-regex", default=None,
                        help="only tests whose name matches this regex, as ctest -R")
    parser.add_argument("--no-build", action="store_true",
                        help="skip building; still refuse a binary older than its sources")
    parser.add_argument("--list", action="store_true",
                        help="list the tests, their binaries and their inputs, then stop")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="name the newest input of every binary")
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()

    # A relative path is read against the working directory first, then against
    # the repository, so `--build-dir build` works from anywhere in the tree.
    if not (build_dir / "CMakeCache.txt").is_file() and not Path(args.build_dir).is_absolute():
        build_dir = (ROOT / args.build_dir).resolve()

    if not (build_dir / "CMakeCache.txt").is_file():
        return die(f"{build_dir} is not a configured build tree (no CMakeCache.txt).\n\n"
                   f"  cmake -B {args.build_dir} -DCMAKE_POLICY_VERSION_MINIMUM=3.5\n")

    src_root = source_root(build_dir)
    roots = (src_root, build_dir)
    wanted = re.compile(args.tests_regex) if args.tests_regex else None

    def select(found: list[Test]) -> list[Test]:
        return [t for t in found if wanted.search(t.name)] if wanted else found

    # First pass: names and, for a tree that has been built before, paths. A
    # tree that has never been built lists the tests but not their commands -
    # everything here works from names and re-resolves afterwards.
    tests = select(discover(build_dir, args.config))

    if not tests:
        return die(f"no test matches {args.tests_regex!r}")

    targets = targets_for(tests)

    def tlog_dir(test: Test) -> Path:
        target = test.exe.stem if test.exe else test.name
        return build_dir / f"{target}.dir" / args.config / f"{target}.tlog"

    if args.list:
        print(f"run-checks: {len(tests)} test(s) in {build_dir} ({args.config}), "
              f"{len(targets)} build target(s)")
        print(f"            sources: {src_root}\n")

        for test in tests:
            if test.exe is None or not test.exe.is_file():
                print(f"  {test.name:<20} {'not built yet':<24}")
                continue

            found = newest_input(test.exe, tlog_dir(test), roots)
            inputs = (f"{found.count} input(s)" + (" (approximate)" if found.approximate else \
                      "")) if found else "no dependency log"
            print(f"  {test.name:<20} {test.exe.name:<24} {inputs}")

            if args.verbose and found:
                print(f"  {'':<20} newest: {rel(found.newest, roots)}  {stamp(found.mtime)}")

        print(f"\n  building: {' '.join(targets)}")
        return EXIT_OK

    # ---- build ------------------------------------------------------------
    if not args.no_build:
        print(f"run-checks: {build_dir}")
        print(f"            sources: {src_root}")
        print(f"            building {len(targets)} target(s) - "
              f"{args.config}, {', '.join(targets)}\n")

        cmd = ["cmake", "--build", str(build_dir), "--config", args.config,
               "--target", *targets]
        if args.parallel:
            cmd += ["--parallel", str(args.parallel)]

        try:
            code, lines = run_stream(cmd)
        except FileNotFoundError:
            return die("cmake is not on PATH - install CMake, or add it to PATH")

        errors = [line for line in lines if BUILD_ERROR_RE.search(line)]
        warnings = [line for line in lines if BUILD_WARNING_RE.search(line)]

        # Both halves matter. A non-zero exit is the ordinary failure; a zero
        # exit alongside a diagnostic line is the one that got past a person
        # reading a truncated build log, and it costs one comparison to catch.
        if code != 0 or errors:
            why = f"the build failed (cmake --build exited {code})" if code != 0 else \
                  "the build reported an error but exited 0"
            print(f"\nrun-checks: {why}.")

            if errors:
                # MSBuild repeats the project path in brackets on every line, and
                # one bad line can produce a dozen diagnostics. Strip the noise
                # and collapse the repeats, so this reads as a list of problems.
                compact: list[str] = []

                for line in errors:
                    trimmed = re.sub(r"\s*\[[^\[\]]*\.(?:vcxproj|sln|targets)\]\s*$", "",
                                     line.strip())
                    if trimmed not in compact:
                        compact.append(trimmed)

                print(f"\n  {len(errors)} diagnostic line(s), {len(compact)} distinct:")

                for line in compact[:15]:
                    print(f"    {line}")

                if len(compact) > 15:
                    print(f"    ... and {len(compact) - 15} more")

            print(f"\n  Nothing is being run: a failed build leaves the previous binaries in\n"
                  f"  place, and running them reports on code that no longer exists. Fix the\n"
                  f"  errors above, then run this again.")
            return EXIT_BUILD

        # Second pass: the executables exist now, so ctest can resolve them, and
        # a path resolved by ctest beats one read out of the testfile.
        tests = select(discover(build_dir, args.config))

        print(f"\n  built: {len(targets)} target(s), "
              f"{len(errors)} error(s), {len(warnings)} warning(s)")

    # ---- is every binary current? -----------------------------------------
    print(f"\nrun-checks: checking {len(tests)} binar{'y' if len(tests) == 1 else 'ies'} "
          f"against the sources they were built from")

    stale: list[tuple[Test, Path, float, float]] = []
    missing: list[Test] = []
    approximate: list[str] = []
    checked = 0
    inputs = 0

    for test in tests:
        if test.exe is None or not test.exe.is_file():
            missing.append(test)
            continue

        found = newest_input(test.exe, tlog_dir(test), roots)

        if found is None:
            print(f"  {test.name:<20} no dependency information - cannot prove it is current")
            continue

        inputs += found.count
        checked += 1

        if found.approximate:
            approximate.append(test.name)

        if args.verbose:
            print(f"  {test.name:<20} {found.count} input(s), newest "
                  f"{rel(found.newest, roots)}  {stamp(found.mtime)}"
                  + (", approximate: this generator writes no dependency log"
                     if found.approximate else "")
                  + (f", {found.skipped} toolchain file(s) ignored" if found.skipped else ""))

        if test.exe.stat().st_mtime + MTIME_TOLERANCE_S < found.mtime:
            stale.append((test, found.newest, found.mtime, test.exe.stat().st_mtime))

    if missing or stale:
        print("\nrun-checks: refusing to run - these binaries are not up to date.\n")

        for test in missing:
            print(f"  missing   {test.name}\n"
                  f"            {test.exe or '(no path: this test is not built yet)'}\n"
                  f"            the build produced no executable for this test\n")

        for test, newest, newest_mtime, exe_mtime in stale:
            print(f"  stale     {test.name}\n"
                  f"            binary: {rel(test.exe, roots)}  {stamp(exe_mtime)}\n"
                  f"            newer:  {rel(newest, roots)}  {stamp(newest_mtime)}\n")

        print("  An executable older than the source it tests reports on the old code, and\n"
              "  it looks exactly like a pass - so it is not run. Rebuild just these:\n\n"
              f"    cmake --build {args.build_dir} --config {args.config} --target "
              f"{' '.join(targets_for([t for t, _, _, _ in stale] + missing)) or '<target>'}\n")

        return EXIT_STALE

    unproven = len(tests) - checked
    print(f"  {checked}/{len(tests)} verified against {inputs} input file(s)"
          + (f"; {unproven} without dependency information" if unproven else ""))

    if approximate:
        print(f"  note: no dependency log for {', '.join(approximate)} - compared\n"
              f"        against tests/ and CMakeLists.txt instead, so an edit to a\n"
              f"        header or a source file this test compiles may not be seen")


    # ---- run --------------------------------------------------------------
    print(f"\nrun-checks: running {len(tests)} test(s)\n")

    cmd = ["ctest", "--test-dir", str(build_dir), "-C", args.config, "--output-on-failure"]
    if args.tests_regex:
        cmd += ["-R", args.tests_regex]

    try:
        code, _ = run_stream(cmd)
    except FileNotFoundError:
        return die("ctest is not on PATH - install CMake, or add it to PATH")

    if code != 0:
        print(f"\nrun-checks: tests failed (ctest exited {code}).\n\n"
              f"  One at a time, with its output:\n"
              f"    ctest --test-dir {args.build_dir} -C {args.config} -R <name> "
              f"--output-on-failure\n")
        return EXIT_TESTS

    return EXIT_OK


if __name__ == "__main__":
    raise SystemExit(main())
