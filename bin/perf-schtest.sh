#!/bin/bash

BIN_DIR=$(dirname ${BASH_SOURCE[0]})
$BIN_DIR/perf-schtest/perf-schtest-common.sh "$@"
export results_dir=results
perf script --script perf-script-schtest.py
