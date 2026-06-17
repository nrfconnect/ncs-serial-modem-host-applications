#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
set -eu

ROOT="${1:-.}"
TEST_ID="${2:?test id required}"

python3 - "$ROOT" "$TEST_ID" <<'PY'
import json
import sys
from pathlib import Path

import yaml

root = Path(sys.argv[1])
test_id = sys.argv[2]
catalog = yaml.safe_load((root / ".github/test/tests.yml").read_text(encoding="utf-8"))

for test in catalog.get("tests", []):
    if test.get("id") == test_id:
        print(json.dumps(test))
        break
else:
    raise SystemExit(f"test id not found: {test_id}")
PY
