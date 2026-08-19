"""Host-side protocol vectors; run with the Python bundled in the workspace."""

from __future__ import annotations

import json
import struct


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def make_frame() -> bytes:
    payload = struct.pack("<iIIHII", -1250, 52340, 123450, 0, 7, 4242)
    body = bytes((1, 1)) + struct.pack("<H", len(payload)) + payload
    return b"\xaa\x55" + body + struct.pack("<H", crc16_ccitt(body))


def main() -> None:
    frame = make_frame()
    assert len(frame) == 30
    assert frame[:2] == b"\xaa\x55"
    assert struct.unpack_from("<H", frame, 4)[0] == 22
    assert crc16_ccitt(frame[2:28]) == struct.unpack_from("<H", frame, 28)[0]

    decoded = struct.unpack_from("<iIIHII", frame, 6)
    assert decoded == (-1250, 52340, 123450, 0, 7, 4242)
    assert json.loads(
        '{"device_id":"env-monitor-001","ts_ms":4242,"temperature_c":-1.250,'
        '"humidity_rh":52.340,"illuminance_lux":123.450,"status":"ok"}'
    )["temperature_c"] == -1.25
    print("protocol vectors: PASS")


if __name__ == "__main__":
    main()
