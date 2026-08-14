#!/usr/bin/env python3
"""Verify XIAO local-only sensor reports over Meshtastic PhoneAPI."""

from __future__ import annotations

import argparse
import collections
import json
import time

from pubsub import pub
from meshtastic.serial_interface import SerialInterface


PREFIXES = {
    "TSV_RADAR_V1,": "radar",
    "TSV_IMU_V1,": "imu",
    "TSV_AUDIO_V1,": "audio",
    "TSV_GPS_V1,": "gps",
}


def payload_text(packet: dict) -> str:
    decoded = packet.get("decoded") or {}
    text = decoded.get("text")
    if isinstance(text, str):
        return text
    payload = decoded.get("payload")
    if isinstance(payload, (bytes, bytearray)):
        return bytes(payload).decode("utf-8", errors="replace")
    return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--seconds", type=float, default=12.0)
    args = parser.parse_args()

    counts: collections.Counter[str] = collections.Counter()
    first: dict[str, str] = {}

    def on_receive(packet: dict, interface=None) -> None:
        text = payload_text(packet)
        for prefix, name in PREFIXES.items():
            if text.startswith(prefix):
                counts[name] += 1
                first.setdefault(name, text)
                break

    pub.subscribe(on_receive, "meshtastic.receive")
    started = time.monotonic()
    interface = SerialInterface(devPath=args.port)
    try:
        while time.monotonic() - started < args.seconds:
            time.sleep(0.05)
    finally:
        interface.close()

    elapsed = max(0.001, time.monotonic() - started)
    result = {
        "port": args.port,
        "elapsedSeconds": round(elapsed, 2),
        "node": getattr(interface, "myInfo", None).my_node_num
        if getattr(interface, "myInfo", None)
        else None,
        "counts": dict(counts),
        "ratesHz": {
            name: round(count / elapsed, 2)
            for name, count in sorted(counts.items())
        },
        "first": first,
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    required = ("radar", "imu", "audio")
    return 0 if all(counts[name] > 0 for name in required) else 1


if __name__ == "__main__":
    raise SystemExit(main())
