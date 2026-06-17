#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
set -eu

ROOT="${ROOT:-.}"
TEST_FILTER="${TEST_FILTER:-all}"

python3 - "$ROOT" "$TEST_FILTER" <<'PY'
import json
import os
import sys
from pathlib import Path

import yaml

root = Path(sys.argv[1])
test_filter = sys.argv[2]
catalog = yaml.safe_load((root / ".github/test/tests.yml").read_text(encoding="utf-8"))

tests = [t for t in catalog.get("tests", []) if t.get("enabled", True)]
if test_filter != "all":
    tests = [t for t in tests if t.get("id") == test_filter]

matrix = json.dumps(tests)

if github_output := os.environ.get("GITHUB_OUTPUT"):
    with open(github_output, "a", encoding="utf-8") as handle:
        handle.write(f"matrix={matrix}\n")
else:
    print(matrix)
PY
