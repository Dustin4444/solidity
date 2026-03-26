## Intro

[oss-fuzz][1] is Google's fuzzing infrastructure that performs continuous
fuzzing. What this means is that, each and every upstream commit is
automatically fetched by the infrastructure and fuzzed on a daily basis.

## How to build fuzzers?

We have multiple fuzzers, some based on string input and others on protobuf
input. To build them, please do the following:

- Create a local docker image from `Dockerfile.ubuntu.clang.ossfuzz` in the
  `scripts/docker/buildpack-deps` sub-directory. Please note that this step
  is likely to take at least an hour to complete. Therefore, it is recommended
  to do it when you are away from the computer (and the computer is plugged to
  power since we do not want a battery drain).

```
$ docker build -t solidity-ossfuzz-ccache-instrument-evmone \
    -f ./scripts/docker/buildpack-deps/Dockerfile.ubuntu.clang.ossfuzz .
```

- Build the fuzzer binaries via the build script inside docker:

```
$ docker run --rm -v `pwd`:/src/solidity \
    -ti solidity-ossfuzz-ccache-instrument-evmone \
    /src/solidity/scripts/ci/build_ossfuzz.sh
```

## Why the elaborate docker image to build fuzzers?

For the following reasons:

- Fuzzing binaries **must** link against libc++ and not libstdc++
  - This is [because][2] (1) MemorySanitizer (which flags uses of
    uninitialized memory) depends on libc++; and (2) because libc++ is
    instrumented (to check for memory and type errors) and libstdc++ not,
    the former may find more bugs.

- Linking against libc++ requires us to compile everything solidity depends
  on from source (and link these against libc++ as well)

- To reproduce the compiler versions used by upstream oss-fuzz bots, we need
  to reuse their docker image containing the said compiler versions

- Some fuzzers depend on libprotobuf, libprotobuf-mutator, libevmone etc.
  which may not be available locally; even if they were they might not be the
  right versions

## What is LIB_FUZZING_ENGINE?

oss-fuzz contains multiple fuzzer back-ends i.e., fuzzers. Each back-end may
require different linker flags. oss-fuzz builder bot defines the correct
linker flags via a bash environment variable called `LIB_FUZZING_ENGINE`.

For the solidity ossfuzz CI build, we use the libFuzzer back-end. This
back-end requires us to manually set the `LIB_FUZZING_ENGINE` to
`-fsanitize=fuzzer`.

## What does the ossfuzz directory contain?

To help oss-fuzz do this, we (as project maintainers) need to provide the
following:

- test harnesses: C/C++ tests that define the `LLVMFuzzerTestOneInput` API.
  This determines what is to be fuzz tested.
- build infrastructure: (c)make targets per fuzzing binary. Fuzzing requires
  coverage and memory instrumentation of the code to be fuzzed.
- configuration files: These are files with the `.options` extension that are
  parsed by oss-fuzz. The only option that we use currently is the `dictionary`
  option that asks the fuzzing engines behind oss-fuzz to use the specified
  dictionary. The specified dictionary happens to be `solidity.dict`.

`solidity.dict` contains Solidity-specific syntactical tokens that are more
likely to guide the fuzzer towards generating parseable and varied Solidity
input.

To be consistent and aid better evaluation of the utility of the fuzzing
dictionary, we stick to the following rules-of-thumb:
  - Full tokens such as `block.number` are preceded and followed by a
    whitespace
  - Incomplete tokens including function calls such as `msg.sender.send()`
    are abbreviated `.send(` to provide some leeway to the fuzzer to
    synthesize variants such as `address(this).send()`
  - Language keywords are suffixed by a whitespace with the exception of
    those that end a line of code such as `break;` and `continue;`

[1]: https://github.com/google/oss-fuzz
[2]: https://github.com/google/oss-fuzz/issues/1114#issuecomment-360660201

## Executables generated

## EVMOne
- `sol_proto_ossfuzz_evmone` (from `solProtoFuzzer2.cpp`). Generates random
  Solidity via Protobuf using `protoToSol2`, compiles with all 4
  configurations (`{noOpt, opt} x {viaIR=false, viaIR=true}`), deploys on
  evmone, and compares output, logs, and storage across all pairs.
- `sol_proto_ossfuzz_evmone_viair` (from `solProtoFuzzer2.cpp`, compiled with
  `FUZZER_MODE_VIAIR`). Same as above but only tests viaIR configurations,
  skipping the legacy codegen path.
- `yul_proto_ossfuzz_evmone` (from `yulProtoFuzzerEvmone.cpp`). Generates
  random Yul via Protobuf, compiles twice (unoptimized and with the full Yul
  optimizer), deploys both versions on evmone with protobuf-generated
  calldata, and compares output data, logs, and storage.
- `yul_proto_ossfuzz_evmone_ssacfg` (from `yulProtoFuzzerEvmone.cpp`,
  compiled with `FUZZER_MODE_SSACFG`). Compares unoptimized legacy codegen
  vs optimized SSA CFG codegen — tests the newer SSA-based code generation
  backend against the legacy stack-based one. Always uses the latest EVM
  version.
- `stack_reuse_codegen_ossfuzz` (from `StackReuseCodegenFuzzer.cpp`).
  Generates random Yul via Protobuf, compiles with and without stack-reuse
  optimisation (i.e. `optimizeStackAllocation`), executes both via evmone,
  and asserts that the resulting EVM state is identical. The goal is to catch
  miscompilations introduced by the stack-reuse code-generation pass.

## Yul Interpreter
- `yul_diff_ssa_cfg_ossfuzz` (from `yulProto_diff_ossfuzz.cpp`). Generates
  random YUL via Protobuf, compiles with and without optimisation, compares
  via YUL interpreter.
- `strictasm_diff_ossfuzz` (from `strictasm_diff_ossfuzz.cpp`). Interprets
  random characters as strict assembly code, compiles with and without
  optimisation, compares via YUL interpreter.

## Looks for Internal Crash
- `yul_proto_ossfuzz` (from `yulProtoFuzzer.cpp`). Generates random YUL via
  Protobuf, runs different optimizer steps, and hopes it crashes.
- `sol_proto_ossfuzz` (from `solProtoFuzzer.cpp`). Generates random Solidity
  via Protobuf. This is actually generating pre-determined code that returns
  known constants. Runs via evmone (in-memory), asserts that it does not
  revert, test() function must return 0, i.e. all constants must be returned
  correctly.
- `strictasm_opt_ossfuzz` (from `strictasm_opt_ossfuzz.cpp`). Interprets
  random characters as strict assembly code, runs the optimizer, and hopes
  it crashes.
- `strictasm_assembly_ossfuzz` (from `strictasm_assembly_ossfuzz.cpp`).
  Interprets random characters as strict assembly code, assembles it, and
  hopes it crashes.
- `const_opt_ossfuzz` (from `const_opt_ossfuzz.cpp`). Interprets random
  characters as some kind of constants, runs the constant optimizer, and
  hopes it crashes.
- `solc_ossfuzz` (from `solc_ossfuzz.cpp`). Interprets random characters as
  Solidity test case, compiles and hopes it crashes.
- `solc_mutator_ossfuzz` (from `solc_ossfuzz.cpp`). Same as above, but with
  a custom mutator included.

## Debugging fuzzer issues with `sol_debug_runner`

`sol_debug_runner` is a standalone tool for debugging differential testing
failures and internal compiler crashes from `sol_proto_ossfuzz_evmone`. It
takes a `.sol` file and runs it through the same compile-deploy-execute
pipeline as the fuzzer, across all 4 configurations:
`{noOpt, opt} x {viaIR=true, viaIR=false}`. It prints bytecodes, EVM
execution results, logs, and storage for each, then reports which
differential comparisons pass or fail.

### Building

Build using the normal (non-ossfuzz) cmake build in `build-normal`:

```bash
cd build-normal
cmake ..
CCACHE_DISABLE=1 make -j8 sol_debug_runner
```

### Reproducing a fuzzer crash

1. Dump the Solidity source from a crash input:

   ```bash
   PROTO_FUZZER_DUMP_PATH=bad-log.sol \
     ./build/test/tools/ossfuzz/sol_proto_ossfuzz_evmone crash-<hash>
   ```

2. Run the debug tool:

   ```bash
   mkdir -p /tmp/debug-output

   LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
     ./build-normal/test/tools/sol_debug_runner bad-log.sol \
     --output-dir /tmp/debug-output
   ```

3. Check the terminal output. The tool prints per-configuration details
   (bytecode, status, logs, storage) followed by a differential comparison
   section:

   ```
   ========== DIFFERENTIAL COMPARISONS ==========

   --- Comparing noOpt_viaIR=true vs opt_viaIR=true ---
     Status:  MATCH (SUCCESS vs SUCCESS)
     Output:  MATCH
     Logs:    DIFFER
     Storage: MATCH
   ```

4. Inspect files in `--output-dir`:
   - `<config>.bytecode.hex` — compiled bytecode in hex
   - `<config>.log` — full execution details (status, output, logs, storage)

### CLI options

```
./sol_debug_runner <file.sol> [--output-dir <dir>] [--via-ir true|false] [--calldata <hex>] [--quiet]
```

- `<file.sol>` — Solidity source file (positional, required)
- `--output-dir <dir>` — write bytecode and log files here (optional)
- `--via-ir true|false` — initial viaIR setting (default: `true`). The tool
  always tests both values; this controls which is "primary".
- `--calldata <hex>` — extra calldata in hex (e.g. `a0ffba`), appended after
  the `test()` method selector
- `--quiet` — suppress all output except a one-line summary (`OK`,
  `MISMATCH`, or `INTERNAL_ERROR`). Used by the delta debugger.

### Exit codes

| Code | Meaning |
|------|---------|
| 0 | All match — no bug |
| 1 | Differential mismatch found |
| 2 | Normal compilation failure / file error |
| 3 | Internal compiler error (assertion failure, crash) |

## Minimizing issues with minimize_sol_issue.py

`minimize_sol_issue.py` is a delta debugger that minimizes a `.sol` file
(and optionally calldata) while preserving the issue. It uses the ddmin
algorithm and calls `sol_debug_runner --quiet` as its oracle.

It supports two modes:
- **Differential mismatch** (default): minimizes while preserving exit code 1
- **Compiler crash** (`--crash`): minimizes while preserving exit code 3

Each time a successful reduction is found, the tool saves a numbered progress
file (e.g. `bad-log.min.001.sol`, `bad-log.min.002.sol`, ...) so you can
inspect or use intermediate results. In `--crash` mode, it also prints `solc`
repro commands for each progress file.

### Usage

Minimize a differential mismatch:

```bash
LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
  python3 test/tools/minimize_sol_issue.py \
    --runner ./build-normal/test/tools/sol_debug_runner \
    --input bad-log.sol
```

Minimize an internal compiler crash:

```bash
LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
  python3 test/tools/minimize_sol_issue.py \
    --crash \
    --runner ./build-normal/test/tools/sol_debug_runner \
    --input crash.sol
```

With calldata:

```bash
LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
  python3 test/tools/minimize_sol_issue.py \
    --runner ./build-normal/test/tools/sol_debug_runner \
    --input bad-log.sol \
    --calldata a0ffba
```

### CLI options

- `--runner <path>` — path to `sol_debug_runner` binary (required)
- `--input <file.sol>` — Solidity file that triggers the issue (required)
- `--crash` — minimize for internal compiler crash instead of differential
  mismatch
- `--calldata <hex>` — extra calldata hex string
- `--via-ir true|false` — initial viaIR setting (default: `true`)
- `--output <file>` — output file (default: `<input>.min.sol`)
- `--solc <path>` — path to `solc` binary for repro commands (auto-detected
  from `--runner` if not set; only used with `--crash`)
- `--timeout <seconds>` — timeout per `sol_debug_runner` invocation
  (default: 30)

### How it works

1. **Verify** the original input reproduces the issue
2. **Phase 1** — minimize calldata (if provided) using ddmin on byte pairs
3. **Phase 2** — minimize Solidity source at line granularity using ddmin
4. **Phase 3** — minimize Solidity source at character granularity using ddmin
5. **Phase 4** — cleanup: remove blank lines, trim whitespace
6. **Final verification** — confirm the minimized output still reproduces

Progress files are saved after each successful reduction. In `--crash` mode,
each progress file is accompanied by `solc` commands you can copy-paste to
reproduce the crash directly:

```
  -> Saved: bad-log.min.003.sol (245 bytes, 12 lines)
     repro: ./build-normal/solc/solc bad-log.min.003.sol --via-ir
     repro: ./build-normal/solc/solc bad-log.min.003.sol
     repro: ./build-normal/solc/solc bad-log.min.003.sol --optimize --via-ir
     repro: ./build-normal/solc/solc bad-log.min.003.sol --optimize
```

## Debugging Yul fuzzer issues with `yul_debug_runner`

`yul_debug_runner` is the Yul equivalent of `sol_debug_runner`. It reproduces
the `yul_proto_ossfuzz_evmone` and `yul_proto_ossfuzz_evmone_ssacfg` fuzzers'
compile-deploy-execute flow on a `.yul` file. It runs three configurations
(unoptimized, optimized legacy, optimized SSACFG), deploys all on evmone, and
compares output, logs, and storage across all pairs. Always uses the latest
EVM version.

### Building

Build using the normal (non-ossfuzz) cmake build in `build-normal`:

```bash
cd build-normal
cmake ..
CCACHE_DISABLE=1 make -j8 yul_debug_runner
```

### Reproducing a fuzzer crash

1. Dump the Yul source from a crash input:

   ```bash
   PROTO_FUZZER_DUMP_PATH=bad.yul \
     ./build/test/tools/ossfuzz/yul_proto_ossfuzz_evmone crash-<hash>
   ```

2. Run the debug tool:

   ```bash
   LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
     ./build-normal/test/tools/yul_debug_runner bad.yul
   ```

3. Check the terminal output. The tool prints per-configuration details
   (bytecode, status, logs, storage) followed by a differential comparison
   section:

   ```
   ========== DIFFERENTIAL COMPARISONS ==========

   --- Comparing unoptimized vs optimized_legacy ---
     Status:  MATCH (SUCCESS vs SUCCESS)
     Output:  MATCH
     Logs:    DIFFER
     Storage: MATCH

   --- Comparing unoptimized vs optimized_ssacfg ---
     Status:  MATCH (SUCCESS vs SUCCESS)
     Output:  MATCH
     Logs:    MATCH
     Storage: MATCH

   --- Comparing optimized_legacy vs optimized_ssacfg ---
     Status:  MATCH (SUCCESS vs SUCCESS)
     Output:  MATCH
     Logs:    DIFFER
     Storage: MATCH
   ```

4. Optionally write output files:

   ```bash
   LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
     ./build-normal/test/tools/yul_debug_runner bad.yul \
     --output-dir /tmp/debug-output
   ```

### CLI options

```
./yul_debug_runner <file.yul> [--output-dir <dir>] [--calldata <hex>] [--quiet]
```

- `<file.yul>` — Yul source file (positional, required)
- `--output-dir <dir>` — write bytecode and log files here (optional)
- `--calldata <hex>` — calldata in hex (e.g. `a0ffba`), passed to the
  deployed contract
- `--quiet` — suppress all output except a one-line summary (`OK`,
  `MISMATCH`, or `INTERNAL_ERROR`). Used by delta debuggers.

### Exit codes

| Code | Meaning |
|------|---------|
| 0 | All match — no bug |
| 1 | Differential mismatch found |
| 2 | Normal compilation failure / file error |
| 3 | Internal compiler error (assertion failure, crash) |

## Quick corpus check with check_diversity_and_errors.sh

`check_diversity_and_errors.sh` is a convenience wrapper that dumps `.sol`
files from a fuzzer corpus and pipes them through `check_sol_proto_files.py`
in one step. It picks N random corpus entries, runs the fuzzer binary to dump
their Solidity source, compiles them with `solc`, and reports errors + feature
diversity.

### Usage

```bash
# Default fuzzer (sol_proto_ossfuzz_evmone), 300 files:
./check_diversity_and_errors.sh my_corpus_sol_proto_ossfuzz_evmone 300

# Explicit fuzzer binary:
./check_diversity_and_errors.sh my_corpus_sol_proto_ossfuzz_evmone 300 \
  ./build/test/tools/ossfuzz/sol_proto_ossfuzz_evmone

# viaIR variant:
./check_diversity_and_errors.sh my_corpus_sol_proto_ossfuzz_evmone_viair 300 \
  ./build/test/tools/ossfuzz/sol_proto_ossfuzz_evmone_viair
```

### Arguments

| Argument | Description |
|----------|-------------|
| `<corpus_dir>` | Directory containing fuzzer corpus files (required) |
| `<num_files>` | Number of random corpus entries to sample (required) |
| `[fuzzer_binary]` | Path to fuzzer binary (default: `./build/test/tools/ossfuzz/sol_proto_ossfuzz_evmone`) |

The script expects `./build-normal/solc/solc` for compilation checks and
`./test/tools/ossfuzz/check_sol_proto_files.py` for the analysis. Dumped
files go into a temporary directory that is cleaned up automatically.

## Checking generated Solidity with check_sol_proto_files.py

`check_sol_proto_files.py` compiles a directory of generated `.sol` files
with `solc` and reports errors, warnings, and a tally of which language
features appear. It is useful for verifying that the protobuf-to-Solidity
converter (`protoToSol2`) produces valid code and for assessing corpus
coverage.

### Dumping `.sol` files from a corpus

First, dump Solidity source from fuzzer corpus entries:

```bash
mkdir -p tmp
find my_corpus_sol_proto_ossfuzz_evmone/ -maxdepth 1 -type f -print0 \
  | shuf -z -n 200 \
  | while IFS= read -r -d '' file; do
      PROTO_FUZZER_DUMP_PATH="tmp/$(basename "$file").sol" \
        ./build/test/tools/ossfuzz/sol_proto_ossfuzz_evmone "$file"
    done
```

### Running the checker

```bash
python3 test/tools/ossfuzz/check_sol_proto_files.py tmp/ \
  --solc ./build-normal/solc/solc
```

### CLI options

- `<sol_dir>` — directory containing `.sol` files (positional, required)
- `--solc <path>` — path to `solc` binary (default: `solc`)
- `--no-compile` — skip compilation, only tally features
- `--max-files <N>` — process at most N files (default: all)

### Output

The tool prints two sections:

**Compilation results** — total errors, warnings, and breakdowns by type.
Files with errors are listed with their first few error messages. Example:

```
============================================================
COMPILATION RESULTS
============================================================
Files compiled:  50
Files with errors: 0
Total errors:    0
Total warnings:  104

Warning types:
  This is a pre-release compiler version, ...: 50
  Unused function parameter. ...: 22
```

**Feature tally** — counts of language features grouped by category (contract
structure, functions, state variables, types, events/errors, control flow,
expressions, builtins, and new features). For each feature it shows the total
occurrence count and how many files contain it. Example:

```
  New Features (this PR):
    ether_units                     (none)
    indexed_params                  total=     1  files=    1/50
    array_push                      (none)
    returns_two                     total=     2  files=    2/50
    free_functions                  total=    50  files=   34/50
```

Features showing `(none)` indicate the fuzzer corpus hasn't grown large
enough to produce those protobuf field combinations yet — this is normal for
a young corpus.

# Coverage for isoltest/fuzzer

## 1. Reset counters (clean slate)
```
lcov --zerocounters --directory build-normal
```

## 2. Run isoltest (or solc many times, or both — they all accumulate)
```
LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
  ./build-normal/test/tools/isoltest --accept-updates --no-smt
```

## 3. Capture & generate HTML
```
lcov --capture --directory build-normal \
  --output-file coverage.info --ignore-errors inconsistent
lcov --remove coverage.info '/usr/*' '*/test/*' '*/deps/*' \
  --output-file coverage_filtered.info --ignore-errors inconsistent
genhtml coverage_filtered.info \
  --output-directory coverage_html --ignore-errors inconsistent
```

Then open coverage_html/index.html.
