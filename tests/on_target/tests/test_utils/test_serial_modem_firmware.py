# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import os
from unittest.mock import patch

import pytest

from utils.serial_modem_firmware import (
    load_serial_modem_firmware_config,
    resolve_serial_modem_release,
)

STATIC_CONFIG = {
    "upstream_repo": "nrfconnect/ncs-serial-modem",
    "asset_suffix": "_nrf9151dk_extmcu.zip",
    "console_baudrate": 1000000,
}

PREVIEW2_RELEASE = {
    "tag_name": "v2.0.0-preview2",
    "html_url": "https://github.com/nrfconnect/ncs-serial-modem/releases/tag/v2.0.0-preview2",
    "assets": [
        {
            "name": "serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.zip",
            "browser_download_url": (
                "https://github.com/nrfconnect/ncs-serial-modem/releases/download/"
                "v2.0.0-preview2/serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.zip"
            ),
        }
    ],
}

STABLE_HEX_ONLY_RELEASE = {
    "tag_name": "v1.0.1",
    "html_url": "https://github.com/nrfconnect/ncs-serial-modem/releases/tag/v1.0.1",
    "assets": [
        {
            "name": "serial_modem_v1.0.1_nrf9151dk_extmcu.hex",
            "browser_download_url": "https://example.com/v1.0.1.hex",
        }
    ],
}

PREVIEW1_RELEASE = {
    "tag_name": "v2.0.0-preview1",
    "html_url": "https://github.com/nrfconnect/ncs-serial-modem/releases/tag/v2.0.0-preview1",
    "assets": [
        {
            "name": "serial_modem_v2.0.0-preview1_nrf9151dk_extmcu.zip",
            "browser_download_url": "https://example.com/preview1.zip",
        }
    ],
}


@pytest.fixture(autouse=True)
def clear_serial_modem_release_env(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("SERIAL_MODEM_RELEASE", raising=False)


def test_resolve_latest_prefers_newest_extmcu_zip() -> None:
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=[PREVIEW2_RELEASE, STABLE_HEX_ONLY_RELEASE, PREVIEW1_RELEASE],
    ):
        resolved = resolve_serial_modem_release()

    assert resolved["release"] == "v2.0.0-preview2"
    assert resolved["bundle"] == "serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.zip"
    assert resolved["hex"] == "serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.hex"


def test_resolve_skips_hex_only_releases() -> None:
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=[STABLE_HEX_ONLY_RELEASE, PREVIEW1_RELEASE],
    ):
        resolved = resolve_serial_modem_release()

    assert resolved["release"] == "v2.0.0-preview1"


def test_resolve_pinned_tag() -> None:
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=PREVIEW1_RELEASE,
    ) as api_request:
        resolved = resolve_serial_modem_release("v2.0.0-preview1")

    api_request.assert_called_once_with(
        "https://api.github.com/repos/nrfconnect/ncs-serial-modem/releases/tags/v2.0.0-preview1"
    )
    assert resolved["release"] == "v2.0.0-preview1"


def test_load_config_uses_serial_modem_release_env(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("SERIAL_MODEM_RELEASE", "v2.0.0-preview1")

    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=PREVIEW1_RELEASE,
    ) as api_request:
        config = load_serial_modem_firmware_config()

    api_request.assert_called_once()
    assert config["release"] == "v2.0.0-preview1"
    assert config["console_baudrate"] == 1000000


def test_resolve_raises_when_no_matching_asset() -> None:
    release_without_asset = {
        "tag_name": "v9.9.9",
        "html_url": "https://example.com/v9.9.9",
        "assets": [{"name": "other.zip", "browser_download_url": "https://example.com/other.zip"}],
    }

    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=[release_without_asset],
    ):
        with pytest.raises(RuntimeError, match="No Serial Modem release"):
            resolve_serial_modem_release()


def test_resolve_pinned_tag_missing_asset() -> None:
    release_without_asset = {
        "tag_name": "v9.9.9",
        "html_url": "https://example.com/v9.9.9",
        "assets": [],
    }

    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=release_without_asset,
    ):
        with pytest.raises(RuntimeError, match="has no asset ending with"):
            resolve_serial_modem_release("v9.9.9")
