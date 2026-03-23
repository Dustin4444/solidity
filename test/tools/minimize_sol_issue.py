#!/usr/bin/env python3
"""
Delta debugger for minimizing Solidity differential testing failures
or internal compiler crashes.

Minimizes the Solidity source file (and optionally calldata) while preserving
the issue detected by sol_debug_runner. Uses the ddmin algorithm.

Usage:
    # Minimize a differential mismatch (default):
    python3 minimize_sol_issue.py \\
        --runner ./build-normal/test/tools/sol_debug_runner \\
        --input bad-log.sol \\
        [--calldata a0ffba] \\
        [--via-ir true] \\
        [--output minimized.sol] \\
        [--timeout 30]

    # Minimize an internal compiler crash:
    python3 minimize_sol_issue.py \\
        --crash \\
        --runner ./build-normal/test/tools/sol_debug_runner \\
        --input crash.sol

Requires LD_LIBRARY_PATH to be set so sol_debug_runner can find libevmone.so.

Exit codes from sol_debug_runner:
    0 = all match (no bug)
    1 = mismatch found (differential bug)
    2 = normal compilation failure
    3 = internal compiler error (assertion failure, crash)
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time


def run_test(runner, sol_file, via_ir, calldata=None, timeout=30):
    """Run sol_debug_runner and return the exit code.

    Returns:
        0 = no mismatch (bug gone)
        1 = mismatch (differential bug reproduced)
        2 = normal compilation failure
        3 = internal compiler error
        -1 = timeout or other failure
    """
    cmd = [runner, sol_file, "--quiet", "--via-ir", str(via_ir).lower()]
    if calldata:
        cmd += ["--calldata", calldata]
    try:
        result = subprocess.run(
            cmd, timeout=timeout,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        return result.returncode
    except subprocess.TimeoutExpired:
        return -1
    except Exception:
        return -1


def check_interesting(runner, sol_content, via_ir, calldata, timeout, tmpdir,
                      target_exit_code):
    """Write sol_content to a temp file and check if the issue reproduces."""
    tmp_path = os.path.join(tmpdir, "candidate.sol")
    with open(tmp_path, "w") as f:
        f.write(sol_content)
    rc = run_test(runner, tmp_path, via_ir, calldata, timeout)
    return rc == target_exit_code


def check_calldata_interesting(runner, sol_file, via_ir, calldata, timeout,
                               target_exit_code):
    """Check if calldata reproduces the issue."""
    rc = run_test(runner, sol_file, via_ir, calldata, timeout)
    return rc == target_exit_code


def ddmin_list(items, test_fn):
    """Standard ddmin algorithm on a list of items.

    test_fn(candidate) should return True if the issue is still reproduced
    with the given candidate list.

    Returns the minimized list.
    """
    n = 2
    while len(items) >= 2:
        chunk_size = max(len(items) // n, 1)
        some_removed = False

        i = 0
        while i < len(items):
            end = min(i + chunk_size, len(items))
            candidate = items[:i] + items[end:]
            if len(candidate) == 0:
                i = end
                continue
            if test_fn(candidate):
                items = candidate
                some_removed = True
                # Don't advance i — the next chunk is now at the same position
            else:
                i = end

        if not some_removed:
            if n >= len(items):
                break
            n = min(n * 2, len(items))

    return items


def minimize_calldata(runner, sol_file, via_ir, calldata_hex, timeout,
                      target_exit_code):
    """Minimize calldata hex string using ddmin on hex character pairs (bytes)."""
    if not calldata_hex:
        return calldata_hex

    # Work on byte pairs
    byte_pairs = [calldata_hex[i:i+2] for i in range(0, len(calldata_hex), 2)]
    # Handle odd-length hex
    if len(calldata_hex) % 2 != 0:
        byte_pairs = list(calldata_hex)  # Fall back to character-level

    print(f"  Calldata: {len(byte_pairs)} units")
    attempts = [0]

    def test_fn(candidate):
        attempts[0] += 1
        cd = "".join(candidate)
        return check_calldata_interesting(
            runner, sol_file, via_ir, cd, timeout, target_exit_code
        )

    result = ddmin_list(byte_pairs, test_fn)
    minimized = "".join(result)
    print(f"  Calldata minimized: {len(byte_pairs)} -> {len(result)} units "
          f"({attempts[0]} attempts)")
    return minimized


def minimize_lines(runner, via_ir, calldata, timeout, tmpdir, lines,
                   target_exit_code):
    """Minimize Solidity source at line granularity using ddmin."""
    print(f"  Line-level: {len(lines)} lines")
    attempts = [0]

    def test_fn(candidate):
        attempts[0] += 1
        content = "\n".join(candidate) + "\n"
        return check_interesting(
            runner, content, via_ir, calldata, timeout, tmpdir, target_exit_code
        )

    result = ddmin_list(lines, test_fn)
    print(f"  Line-level minimized: {len(lines)} -> {len(result)} lines "
          f"({attempts[0]} attempts)")
    return result


def minimize_chars(runner, via_ir, calldata, timeout, tmpdir, content,
                   target_exit_code):
    """Minimize Solidity source at character granularity using ddmin."""
    chars = list(content)
    print(f"  Char-level: {len(chars)} chars")
    attempts = [0]

    def test_fn(candidate):
        attempts[0] += 1
        text = "".join(candidate)
        return check_interesting(
            runner, text, via_ir, calldata, timeout, tmpdir, target_exit_code
        )

    result = ddmin_list(chars, test_fn)
    minimized = "".join(result)
    print(f"  Char-level minimized: {len(chars)} -> {len(result)} chars "
          f"({attempts[0]} attempts)")
    return minimized


def try_remove_blank_lines(runner, via_ir, calldata, timeout, tmpdir, lines,
                           target_exit_code):
    """Try removing blank/whitespace-only lines one at a time."""
    changed = True
    while changed:
        changed = False
        for i in range(len(lines)):
            if lines[i].strip() == "":
                candidate = lines[:i] + lines[i+1:]
                content = "\n".join(candidate) + "\n"
                if check_interesting(runner, content, via_ir, calldata,
                                     timeout, tmpdir, target_exit_code):
                    lines = candidate
                    changed = True
                    break
    return lines


def try_trim_whitespace(runner, via_ir, calldata, timeout, tmpdir, lines,
                        target_exit_code):
    """Try reducing leading whitespace on each line."""
    for i in range(len(lines)):
        stripped = lines[i].lstrip()
        if stripped != lines[i]:
            candidate = lines[:i] + [stripped] + lines[i+1:]
            content = "\n".join(candidate) + "\n"
            if check_interesting(runner, content, via_ir, calldata,
                                 timeout, tmpdir, target_exit_code):
                lines = candidate
    return lines


EXIT_CODE_NAMES = {
    0: "all match (no bug)",
    1: "mismatch (differential bug)",
    2: "normal compilation failure",
    3: "internal compiler error (crash)",
    -1: "timeout",
}


def main():
    parser = argparse.ArgumentParser(
        description="Delta-debug minimize a Solidity differential testing "
                    "failure or compiler crash"
    )
    parser.add_argument("--runner", required=True,
                        help="Path to sol_debug_runner binary")
    parser.add_argument("--input", required=True,
                        help="Input Solidity file that triggers the issue")
    parser.add_argument("--crash", action="store_true",
                        help="Minimize for internal compiler crash (exit 3) "
                             "instead of differential mismatch (exit 1)")
    parser.add_argument("--calldata", default="",
                        help="Extra calldata hex string")
    parser.add_argument("--via-ir", default="true",
                        help="Initial viaIR setting (true/false)")
    parser.add_argument("--output", default="",
                        help="Output file for minimized Solidity "
                             "(default: <input>.min.sol)")
    parser.add_argument("--timeout", type=int, default=30,
                        help="Timeout per sol_debug_runner invocation in "
                             "seconds (default: 30)")

    args = parser.parse_args()

    runner = os.path.abspath(args.runner)
    input_file = os.path.abspath(args.input)
    via_ir = args.via_ir
    calldata = args.calldata if args.calldata else None
    timeout = args.timeout
    target_exit_code = 3 if args.crash else 1
    mode_name = "compiler crash" if args.crash else "differential mismatch"

    if args.output:
        output_file = args.output
    else:
        base, ext = os.path.splitext(input_file)
        output_file = base + ".min" + ext

    if not os.path.isfile(runner):
        print(f"Error: runner not found: {runner}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(input_file):
        print(f"Error: input file not found: {input_file}", file=sys.stderr)
        sys.exit(1)

    with open(input_file, "r") as f:
        original_source = f.read()

    print(f"Mode: {mode_name} (looking for exit code {target_exit_code})")
    print(f"Input: {input_file} ({len(original_source)} bytes)")
    if calldata:
        print(f"Calldata: {calldata}")
    print(f"via-ir: {via_ir}")
    print()

    # Step 0: Verify the original reproduces the issue
    print("Step 0: Verifying original reproduces the issue...")
    rc = run_test(runner, input_file, via_ir, calldata, timeout)
    if rc != target_exit_code:
        rc_name = EXIT_CODE_NAMES.get(rc, f"unknown ({rc})")
        print(f"Error: original input does not reproduce the issue")
        print(f"  Expected exit code {target_exit_code} ({mode_name})")
        print(f"  Got exit code {rc} ({rc_name})")
        sys.exit(1)
    print(f"  Issue reproduced (exit code {target_exit_code}). "
          f"Starting minimization.\n")

    start_time = time.time()

    with tempfile.TemporaryDirectory(prefix="sol_minimize_") as tmpdir:
        # Phase 1: Minimize calldata
        if calldata:
            print("Phase 1: Minimizing calldata...")
            calldata = minimize_calldata(
                runner, input_file, via_ir, calldata, timeout,
                target_exit_code
            )
            if not calldata:
                # Check if empty calldata still reproduces
                rc = run_test(runner, input_file, via_ir, None, timeout)
                if rc == target_exit_code:
                    print("  Calldata can be removed entirely!")
                    calldata = None
                else:
                    print("  Warning: empty calldata doesn't reproduce; "
                          "keeping minimal")
            print()

        # Phase 2: Minimize Solidity source — line level
        print("Phase 2: Minimizing Solidity source (line-level)...")
        lines = original_source.splitlines()
        lines = minimize_lines(
            runner, via_ir, calldata, timeout, tmpdir, lines,
            target_exit_code
        )
        print()

        # Phase 3: Minimize Solidity source — character level
        print("Phase 3: Minimizing Solidity source (character-level)...")
        content = "\n".join(lines) + "\n"
        content = minimize_chars(
            runner, via_ir, calldata, timeout, tmpdir, content,
            target_exit_code
        )
        print()

        # Phase 4: Cleanup — remove blank lines and trim whitespace
        print("Phase 4: Cleanup (blank lines, whitespace)...")
        lines = content.splitlines()
        lines = try_remove_blank_lines(
            runner, via_ir, calldata, timeout, tmpdir, lines,
            target_exit_code
        )
        lines = try_trim_whitespace(
            runner, via_ir, calldata, timeout, tmpdir, lines,
            target_exit_code
        )
        content = "\n".join(lines) + "\n"
        print(f"  Final: {len(lines)} lines, {len(content)} chars")
        print()

        # Final verification
        print("Final verification...")
        tmp_path = os.path.join(tmpdir, "candidate.sol")
        with open(tmp_path, "w") as f:
            f.write(content)
        rc = run_test(runner, tmp_path, via_ir, calldata, timeout)
        if rc != target_exit_code:
            rc_name = EXIT_CODE_NAMES.get(rc, f"unknown ({rc})")
            print(f"Warning: final result does not reproduce "
                  f"(exit code {rc}: {rc_name})!")
            print("Falling back to last known good version.")
            content = "\n".join(lines) + "\n"
        else:
            print(f"  Verified (exit code {target_exit_code}).")

    elapsed = time.time() - start_time

    # Write output
    with open(output_file, "w") as f:
        f.write(content)

    print()
    print(f"Minimized: {len(original_source)} -> {len(content)} bytes "
          f"({len(content)*100//max(len(original_source),1)}%)")
    if calldata is not None:
        print(f"Minimized calldata: {calldata}")
    print(f"Written to: {output_file}")
    print(f"Time: {elapsed:.1f}s")


if __name__ == "__main__":
    main()
