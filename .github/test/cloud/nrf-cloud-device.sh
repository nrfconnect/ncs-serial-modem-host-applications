#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
# nRF Cloud device management for on-target provisioning tests.
#
# delete-if-exists only removes the single device ID passed on the command line
# after validating it against CI_NRF54L15_DEVICE_ID. It never bulk-deletes.
set -eu

: "${NRF_CLOUD_API_KEY:?NRF_CLOUD_API_KEY is required}"
: "${CI_NRF54L15_DEVICE_ID:?CI_NRF54L15_DEVICE_ID is required for delete-if-exists}"

API_HOST="${NRF_CLOUD_API_HOST:-https://api.nrfcloud.com/v1}"

normalize_device_id() {
	printf '%s' "$1" | tr '[:lower:]' '[:upper:]'
}

validate_dut_device_id() {
	local device_id="$1"
	local normalized expected

	normalized="$(normalize_device_id "${device_id}")"
	expected="$(normalize_device_id "${CI_NRF54L15_DEVICE_ID}")"

	if ! printf '%s' "${normalized}" | grep -Eq '^[0-9A-F]{16}$'; then
		echo "::error::Refusing nRF Cloud operation: invalid device ID '${device_id}'" >&2
		return 1
	fi

	if [ "${normalized}" != "${expected}" ]; then
		echo "::error::Refusing delete: device ID ${normalized}" \
			" does not match configured DUT allowlist ${expected}" >&2
		return 1
	fi

	printf '%s' "${normalized}"
}

device_url() {
	printf '%s/devices/%s' "${API_HOST}" "$1"
}

device_exists() {
	local device_id="$1"
	local status

	status="$(curl -sS -o /dev/null -w '%{http_code}' \
		-H "Authorization: Bearer ${NRF_CLOUD_API_KEY}" \
		"$(device_url "${device_id}")")"

	case "${status}" in
	200) return 0 ;;
	404) return 1 ;;
	*)
		echo "::error::Unexpected status ${status} checking device ${device_id}" >&2
		return 2
		;;
	esac
}

cmd_delete_if_exists() {
	local device_id="$1"
	local validated

	validated="$(validate_dut_device_id "${device_id}")"

	if ! device_exists "${validated}"; then
		echo "DUT ${validated} is not registered in nRF Cloud; nothing to delete"
		return 0
	fi

	echo "Deleting only the configured DUT ${validated} from nRF Cloud"
	curl -sS -f -X DELETE \
		-H "Authorization: Bearer ${NRF_CLOUD_API_KEY}" \
		"$(device_url "${validated}")"
	echo "DUT ${validated} deleted"
}

cmd_onboard() {
	local csv_file="$1"

	if [ ! -f "${csv_file}" ]; then
		echo "::error::Onboarding CSV not found: ${csv_file}" >&2
		return 1
	fi

	echo "Onboarding DUT from ${csv_file}"
	nrf_cloud_onboard --api-key "${NRF_CLOUD_API_KEY}" --csv "${csv_file}"
}

usage() {
	echo "Usage: $0 {delete-if-exists|onboard} ..." >&2
	exit 1
}

case "${1:-}" in
delete-if-exists)
	[ "$#" -eq 2 ] || usage
	cmd_delete_if_exists "$2"
	;;
onboard)
	[ "$#" -eq 2 ] || usage
	cmd_onboard "$2"
	;;
*)
	usage
	;;
esac
