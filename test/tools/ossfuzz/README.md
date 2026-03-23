## Intro

[oss-fuzz][1] is Google's fuzzing infrastructure that performs continuous fuzzing. What this means is that, each and every upstream commit is automatically fetched by the infrastructure and fuzzed on a daily basis.

## How to build fuzzers?

We have multiple fuzzers, some based on string input and others on protobuf input. To build them, please do the following:

- Create a local docker image from `Dockerfile.ubuntu.clang.ossfuzz` in the `.circleci/docker` sub-directory. Please note that this step is likely to take at least an hour to complete. Therefore, it is recommended to do it when you are away from the computer (and the computer is plugged to power since we do not want a battery drain).

```
$ cd .circleci/docker
$ docker build -t solidity-ossfuzz-local -f Dockerfile.ubuntu.clang.ossfuzz .
```

- Login to the docker container sourced from the image built in the previous step from the solidity parent directory

```
## Host
$ cd solidity
$ docker run -v `pwd`:/src/solidity -ti solidity-ossfuzz-local /bin/bash
## Docker shell
$ cd /src/solidity
```

- Run cmake and build fuzzer harnesses

```
## Docker shell
$ cd /src/solidity
$ rm -rf fuzzer-build && mkdir fuzzer-build && cd fuzzer-build
## Compile protobuf C++ bindings
$ protoc --proto_path=../test/tools/ossfuzz yulProto.proto --cpp_out=../test/tools/ossfuzz
$ protoc --proto_path=../test/tools/ossfuzz abiV2Proto.proto --cpp_out=../test/tools/ossfuzz
$ protoc --proto_path=../test/tools/ossfuzz solProto.proto --cpp_out=../test/tools/ossfuzz
## Run cmake
$ export CC=clang CXX=clang++
$ cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/libfuzzer.cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} ..
$ make ossfuzz ossfuzz_proto ossfuzz_abiv2 -j
```

## Why the elaborate docker image to build fuzzers?

For the following reasons:

- Fuzzing binaries **must** link against libc++ and not libstdc++
  - This is [because][2] (1) MemorySanitizer (which flags uses of uninitialized memory) depends on libc++; and (2) because libc++ is instrumented (to check for memory and type errors) and libstdc++ not, the former may find more bugs.

- Linking against libc++ requires us to compile everything solidity depends on from source (and link these against libc++ as well)

- To reproduce the compiler versions used by upstream oss-fuzz bots, we need to reuse their docker image containing the said compiler versions

- Some fuzzers depend on libprotobuf, libprotobuf-mutator, libevmone etc. which may not be available locally; even if they were they might not be the right versions

## What is LIB\_FUZZING\_ENGINE?

oss-fuzz contains multiple fuzzer back-ends i.e., fuzzers. Each back-end may require different linker flags. oss-fuzz builder bot defines the correct linker flags via a bash environment variable called `LIB_FUZZING_ENGINE`.

For the solidity ossfuzz CI build, we use the libFuzzer back-end. This back-end requires us to manually set the `LIB_FUZZING_ENGINE` to `-fsanitize=fuzzer`.

## What does the ossfuzz directory contain?

To help oss-fuzz do this, we (as project maintainers) need to provide the following:

- test harnesses: C/C++ tests that define the `LLVMFuzzerTestOneInput` API. This determines what is to be fuzz tested.
- build infrastructure: (c)make targets per fuzzing binary. Fuzzing requires coverage and memory instrumentation of the code to be fuzzed.
- configuration files: These are files with the `.options` extension that are parsed by oss-fuzz. The only option that we use currently is the `dictionary` option that asks the fuzzing engines behind oss-fuzz to use the specified dictionary. The specified dictionary happens to be `solidity.dict.`

`solidity.dict` contains Solidity-specific syntactical tokens that are more likely to guide the fuzzer towards generating parseable and varied Solidity input.

To be consistent and aid better evaluation of the utility of the fuzzing dictionary, we stick to the following rules-of-thumb:
  - Full tokens such as `block.number` are preceded and followed by a whitespace
  - Incomplete tokens including function calls such as `msg.sender.send()` are abbreviated `.send(` to provide some leeway to the fuzzer to synthesize variants such as `address(this).send()`
  - Language keywords are suffixed by a whitespace with the exception of those that end a line of code such as `break;` and `continue;`

[1]: https://github.com/google/oss-fuzz
[2]: https://github.com/google/oss-fuzz/issues/1114#issuecomment-360660201

## Executables generated

- `yulProto_diff_ossfuzz.cpp`: exe is `yul_diff_ssa_cfg_ossfuzz`. Generates
  random YUL via Protobuf, compiles with and without optimisation, compares via YUL
  interpreter
- `yulProtoFuzzer.cpp`: exe is`yul_proto_ossfuzz`. Generates random YUL via Protobuf,
  runs different optimizer steps, and hopes it crashes
- `strictasm_diff_ossfuzz.cpp`: exe is `strictasm_diff_ossfuzz`. Interprets random characters
  as strict assembly code, compiles with and without optimisation, compares via
  YUL interpreter
- `strictasm_opt_ossfuzz.cpp`: exe is `strictasm_opt_ossfuzz`. Interprets
  random characters as strict assembly code, runs the optimizer, and hopes it
  crashes
- `strictasm_assembly_ossfuzz.cpp`: exe is `strictasm_assembly_ossfuzz`.
  Interprets random characters as strict assembly code, assembles it, and hopes
  it crashes
- `const_opt_ossfuzz.cpp`: exe is `const_opt_ossfuzz`. Interprets random characters as some kind
  of constants, runs the constant optimizer, and hopes it crashes
- `solProtoFuzzer.cpp` exe is `sol_proto_ossfuzz`. Generates random Solidity via Protobuf.
  This is actually generating pre-determined code that returns known constants.
  Runs via evmone (in-memory), asserts that it does not revert, test() function must return 0,
  i.e. all constants must be returned correctly.
- `solc_ossfuzz.cpp`. exe is`solc_ossfuzz`. Interprets random characters as Solidity test case,
  compiles and hopes it crashes. NOTE: does not work well it seems
- `solc_ossfuzz.cpp`: exe is `solc_mutator_ossfuzz`. Same as above, but with mutator included.
  I sincerely think this mutator thing is junk.
- `StackReuseCodegenFuzzer.cpp`: exe is `stack_reuse_codegen_ossfuzz`. Generates random Yul via
  Protobuf, compiles with and without stack-reuse optimisation (i.e. `optimizeStackAllocation`),
  executes both via evmone, and asserts that the resulting EVM state is identical. The goal is to
  catch miscompilations introduced by the stack-reuse code-generation pass.

## Debugging fuzzer crashes with sol\_debug\_runner

`sol_debug_runner` is a standalone tool for debugging differential testing failures
from `sol_proto2_ossfuzz`. It takes a `.sol` file and runs it through the same
compile-deploy-execute pipeline as the fuzzer, across all 4 configurations:
`{noOpt, opt} x {viaIR=true, viaIR=false}`. It prints bytecodes, EVM execution
results, logs, and storage for each, then reports which differential comparisons
pass or fail.

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
     ./build/test/tools/ossfuzz/sol_proto2_ossfuzz crash-<hash>
   ```

2. Run the debug tool:

   ```bash
   mkdir -p /tmp/debug-output

   LD_LIBRARY_PATH=/home/matesoos/development/evmone/build/lib:$LD_LIBRARY_PATH \
     ./build-normal/test/tools/sol_debug_runner bad-log.sol \
     --output-dir /tmp/debug-output
   ```

3. Check the terminal output. The tool prints per-configuration details (bytecode,
   status, logs, storage) followed by a differential comparison section:

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
./sol_debug_runner <file.sol> [--output-dir <dir>] [--via-ir true|false]
```

- `<file.sol>` — Solidity source file (positional, required)
- `--output-dir <dir>` — write bytecode and log files here (optional)
- `--via-ir true|false` — initial viaIR setting (default: `true`). The tool
  always tests both values; this controls which is "primary".
