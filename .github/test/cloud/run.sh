#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
set -eu

: "${REPO_ROOT:?REPO_ROOT is required}"
: "${TEST_JSON:?TEST_JSON is required}"

mkdir -p "${REPO_ROOT}/build"

pip install --quiet -r "${REPO_ROOT}/tests/on_target/requirements.txt"

export PYTHONPATH="${REPO_ROOT}/tests/on_target${PYTHONPATH:+:${PYTHONPATH}}"

python3 -m pytest \
	"${REPO_ROOT}/tests/on_target/tests/test_cloud/test_cloud_connect.py" \
	-c "${REPO_ROOT}/tests/on_target/tests/pytest.ini" \
	--html="${REPO_ROOT}/build/hardware-pytest-report.html" \
	--self-contained-html \
	-v
