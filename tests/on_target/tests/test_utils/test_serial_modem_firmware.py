# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

from unittest.mock import patch

import pytest

from utils.serial_modem_firmware import (
    clear_serial_modem_release_cache,
    load_serial_modem_firmware_config,
    resolve_serial_modem_release,
)

REPO = "nrfconnect/ncs-serial-modem"

STATIC_CONFIG = {
    "upstream_repo": REPO,
    "asset_suffix": "_nrf9151dk_nrf91m1.zip",
    "console_baudrate": 1000000,
}


def _release(tag: str, *, bundle: bool = True) -> dict:
    """Build a fake upstream release payload for *tag*.

    With *bundle* the release ships the nrf91m1 zip CI looks for; otherwise it
    only ships a bare .hex, which resolution must skip.
    """
    ext = "zip" if bundle else "hex"
    name = f"serial_modem_{tag}_nrf9151dk_nrf91m1.{ext}"
    return {
        "tag_name": tag,
        "html_url": f"https://github.com/{REPO}/releases/tag/{tag}",
        "assets": [
            {
                "name": name,
                "browser_download_url": f"https://example.com/{tag}.{ext}",
            }
        ],
    }


# Fake, version-neutral fixtures: only the presence of the nrf91m1 zip and the
# list order matter to the resolver, not the tag values.
NEWEST_RELEASE = _release("v9.9.9")
OLDER_RELEASE = _release("v9.9.8")
HEX_ONLY_RELEASE = _release("v9.9.7", bundle=False)


@pytest.fixture(autouse=True)
def isolate_resolution(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("SERIAL_MODEM_RELEASE", raising=False)
    clear_serial_modem_release_cache()


def test_resolve_latest_prefers_newest_nrf91m1_zip() -> None:
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=[NEWEST_RELEASE, HEX_ONLY_RELEASE, OLDER_RELEASE],
    ):
        resolved = resolve_serial_modem_release()

    tag = NEWEST_RELEASE["tag_name"]
    assert resolved["release"] == tag
    assert resolved["bundle"] == f"serial_modem_{tag}_nrf9151dk_nrf91m1.zip"
    assert resolved["hex"] == f"serial_modem_{tag}_nrf9151dk_nrf91m1.hex"


def test_resolve_skips_hex_only_releases() -> None:
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=[HEX_ONLY_RELEASE, OLDER_RELEASE],
    ):
        resolved = resolve_serial_modem_release()

    assert resolved["release"] == OLDER_RELEASE["tag_name"]


def test_resolve_pinned_tag() -> None:
    tag = OLDER_RELEASE["tag_name"]
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=OLDER_RELEASE,
    ) as api_request:
        resolved = resolve_serial_modem_release(tag)

    api_request.assert_called_once_with(
        f"https://api.github.com/repos/{REPO}/releases/tags/{tag}"
    )
    assert resolved["release"] == tag


def test_load_config_uses_serial_modem_release_env(monkeypatch: pytest.MonkeyPatch) -> None:
    tag = NEWEST_RELEASE["tag_name"]
    monkeypatch.setenv("SERIAL_MODEM_RELEASE", tag)

    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=NEWEST_RELEASE,
    ) as api_request:
        config = load_serial_modem_firmware_config()

    api_request.assert_called_once()
    assert config["release"] == tag
    assert config["console_baudrate"] == 1000000


def test_load_config_uses_pinned_release_from_yaml() -> None:
    tag = NEWEST_RELEASE["tag_name"]
    config_with_pin = {**STATIC_CONFIG, "pinned_release": tag}

    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=config_with_pin,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=NEWEST_RELEASE,
    ) as api_request:
        config = load_serial_modem_firmware_config()

    api_request.assert_called_once()
    assert config["release"] == tag


def test_repeated_resolution_hits_the_api_once() -> None:
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=[NEWEST_RELEASE],
    ) as api_request:
        first = resolve_serial_modem_release()
        second = resolve_serial_modem_release()

    api_request.assert_called_once()
    assert first == second


def test_cached_release_is_not_mutated_by_callers() -> None:
    with patch(
        "utils.serial_modem_firmware.load_serial_modem_static_config",
        return_value=STATIC_CONFIG,
    ), patch(
        "utils.serial_modem_firmware._github_api_request",
        return_value=[NEWEST_RELEASE],
    ):
        resolve_serial_modem_release()["release"] = "tampered"
        assert resolve_serial_modem_release()["release"] == NEWEST_RELEASE["tag_name"]


def test_resolve_raises_when_no_matching_asset() -> None:
    release_without_asset = {
        "tag_name": "v9.9.0",
        "html_url": "https://example.com/v9.9.0",
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
        "tag_name": "v9.9.0",
        "html_url": "https://example.com/v9.9.0",
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
            resolve_serial_modem_release("v9.9.0")
