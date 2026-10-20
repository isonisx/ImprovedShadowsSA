#!/usr/bin/env bash
set -euo pipefail

LC_ALL=C make -C build config=debug -Bnw | bear parse-sh
make -C build config=release
