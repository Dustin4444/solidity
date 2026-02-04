#!/usr/bin/env bash
set -eo pipefail

# This is a regression test verifying that the compiler does not generate a tuple conversion
# function when both sides of a ternary operator have tuple operands of identical types.
# See https://github.com/argotorg/solidity/issues/16066

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

output=$("$SOLC" "${SCRIPT_DIR}/inputs.sol" --ir)

[[ ! "$output" =~ "function convert_t_tuple" ]] ||
    fail "Generated IR should not contain a tuple conversion function."
