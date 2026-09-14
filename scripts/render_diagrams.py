#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Render the applications' PlantUML diagram sources to SVG.

With no arguments, every ``applications/*/doc/diagrams/*.puml`` file is
rendered to an ``.svg`` file of the same name. Rendering is done by a PlantUML
server, so no local Java installation is needed. Point ``--server`` at a local
server to avoid the public one:

    docker run -d -p 8080:8080 plantuml/plantuml-server:jetty
    scripts/render_diagrams.py --server http://localhost:8080
"""

from __future__ import annotations

import argparse
import sys
import urllib.error
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIAGRAM_GLOB = "applications/*/doc/diagrams/*.puml"
DEFAULT_SERVER = "https://www.plantuml.com/plantuml"
TIMEOUT_SECONDS = 60

# PlantUML's own base64 variant, used to put a diagram source in a URL.
ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"


def encode(source: str) -> str:
    """Deflate and encode a diagram source the way PlantUML servers expect."""
    # Strip the zlib header and checksum to get a raw deflate stream.
    data = zlib.compress(source.encode("utf-8"), 9)[2:-4]
    encoded = []

    for offset in range(0, len(data), 3):
        group = data[offset : offset + 3]
        padded = group + bytes(3 - len(group))
        word = (padded[0] << 16) | (padded[1] << 8) | padded[2]
        encoded += [
            ALPHABET[(word >> 18) & 0x3F],
            ALPHABET[(word >> 12) & 0x3F],
            ALPHABET[(word >> 6) & 0x3F],
            ALPHABET[word & 0x3F],
        ]

    return "".join(encoded)


def render(source: str, server: str) -> bytes:
    """Return the SVG for a diagram source, or raise SystemExit on a syntax error."""
    url = f"{server.rstrip('/')}/svg/{encode(source)}"
    request = urllib.request.Request(url, headers={"User-Agent": "render_diagrams.py"})

    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
            svg = response.read()
            error = response.headers.get("X-PlantUML-Diagram-Error")
    except urllib.error.HTTPError as err:
        # A diagram that does not parse comes back as 400 with the error in a header.
        error = err.headers.get("X-PlantUML-Diagram-Error")
        line = err.headers.get("X-PlantUML-Diagram-Error-Line")
        if error is None:
            raise SystemExit(f"Server returned {err.code} {err.reason}: {url}") from err
        raise SystemExit(f"{error} on line {line}") from err
    except urllib.error.URLError as err:
        raise SystemExit(f"Cannot reach {server}: {err.reason}") from err

    if error is not None:
        raise SystemExit(error)

    return svg


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument(
        "sources",
        nargs="*",
        type=Path,
        help=f"Diagram sources to render (default: {DIAGRAM_GLOB})",
    )
    parser.add_argument(
        "--server",
        default=DEFAULT_SERVER,
        help=f"PlantUML server to render with (default: {DEFAULT_SERVER})",
    )
    args = parser.parse_args(argv)

    sources = args.sources or sorted(ROOT.glob(DIAGRAM_GLOB))
    if not sources:
        raise SystemExit(f"No diagram sources found in {ROOT / DIAGRAM_GLOB}")

    for source in sources:
        if not source.is_file():
            raise SystemExit(f"Diagram source not found: {source}")

        svg = render(source.read_text(), args.server)
        target = source.with_suffix(".svg")
        target.write_bytes(svg)

        try:
            shown = target.relative_to(ROOT)
        except ValueError:
            shown = target
        print(f"{shown} ({len(svg)} bytes)")


if __name__ == "__main__":
    sys.exit(main())
