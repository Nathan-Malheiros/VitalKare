"""
VK Protocol — VitalKare Wire Protocol constants, CRC, frame builder/parser.

Wire layout (VK_HEADER_SIZE = 9):
  [0x56][0x4B][version=0x01][pkt_type]
  [payload_len_b0][payload_len_b1][payload_len_b2][payload_len_b3]  <- uint32 LE
  [seq_lo]                                                           <- byte 8
  [payload_0 ... payload_n-1]                                        <- starts at byte 9
  [crc_lo][crc_hi]                                                   <- uint16 LE

CRC-16/CCITT covers bytes 0..8+payload_len (9 + payload_len bytes).
seq_hi (byte 9) is clobbered by payload[0] in the firmware — only seq_lo is reliable.
"""

from __future__ import annotations
import struct
from dataclasses import dataclass, field
from typing import Optional

# ────────────────────────────────────────────
# Constants
# ────────────────────────────────────────────
MAGIC_0: int = 0x56  # 'V'
MAGIC_1: int = 0x4B  # 'K'
VK_VERSION: int = 0x01
HEADER_SIZE: int = 9
CRC_SIZE: int = 2
MIN_FRAME_SIZE: int = HEADER_SIZE + CRC_SIZE  # 11
MAX_PAYLOAD_SIZE: int = 8192
MAX_FRAME_SIZE: int = HEADER_SIZE + MAX_PAYLOAD_SIZE + CRC_SIZE


# ────────────────────────────────────────────
# Packet type codes
# ────────────────────────────────────────────
class Pkt:
    # Connection
    HANDSHAKE_HELLO = 0x01
    HANDSHAKE_ACK   = 0x02
    HEARTBEAT_PING  = 0x03
    HEARTBEAT_PONG  = 0x04

    # Patient events
    EVENT_REQUEST   = 0x10
    EVENT_EMERGENCY = 0x11
    EVENT_ACK       = 0x12
    EVENT_CANCEL    = 0x13

    # Audio
    AUDIO_SESSION_START = 0x20
    AUDIO_CHUNK         = 0x21
    AUDIO_SESSION_END   = 0x22

    # Telemetry
    TELEMETRY_ENV      = 0x30
    TELEMETRY_MOTION   = 0x31
    TELEMETRY_OXIMETER = 0x32
    TELEMETRY_SYSTEM   = 0x33

    # Image
    IMAGE_FRAME        = 0x40
    IMAGE_STREAM_START = 0x41
    IMAGE_STREAM_STOP  = 0x42

    # Commands (server → device)
    CMD_DISPLAY_TEXT      = 0x50
    CMD_DISPLAY_CLEAR     = 0x51
    CMD_BUZZER            = 0x52
    CMD_LED               = 0x53
    CMD_REQUEST_IMAGE     = 0x54
    CMD_REQUEST_TELEMETRY = 0x55

    # OTA / system
    OTA_START  = 0x60
    OTA_CHUNK  = 0x61
    OTA_COMMIT = 0x62
    OTA_ABORT  = 0x63
    CMD_REBOOT = 0x6F

    ERROR = 0xFF


PKT_NAMES: dict[int, str] = {
    0x01: "HANDSHAKE_HELLO", 0x02: "HANDSHAKE_ACK",
    0x03: "HEARTBEAT_PING",  0x04: "HEARTBEAT_PONG",
    0x10: "EVENT_REQUEST",   0x11: "EVENT_EMERGENCY",
    0x12: "EVENT_ACK",       0x13: "EVENT_CANCEL",
    0x20: "AUDIO_SESSION_START", 0x21: "AUDIO_CHUNK", 0x22: "AUDIO_SESSION_END",
    0x30: "TELEMETRY_ENV",   0x31: "TELEMETRY_MOTION",
    0x32: "TELEMETRY_OXIMETER", 0x33: "TELEMETRY_SYSTEM",
    0x40: "IMAGE_FRAME", 0x41: "IMAGE_STREAM_START", 0x42: "IMAGE_STREAM_STOP",
    0x50: "CMD_DISPLAY_TEXT", 0x51: "CMD_DISPLAY_CLEAR",
    0x52: "CMD_BUZZER", 0x53: "CMD_LED",
    0x54: "CMD_REQUEST_IMAGE", 0x55: "CMD_REQUEST_TELEMETRY",
    0x60: "OTA_START", 0x61: "OTA_CHUNK", 0x62: "OTA_COMMIT", 0x63: "OTA_ABORT",
    0x6F: "CMD_REBOOT", 0xFF: "ERROR",
}


# ────────────────────────────────────────────
# CRC-16/CCITT (poly=0x1021, init=0xFFFF)
# ────────────────────────────────────────────
def crc16_ccitt(data: bytes | bytearray) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


# ────────────────────────────────────────────
# Frame
# ────────────────────────────────────────────
@dataclass
class VKFrame:
    pkt_type: int
    seq: int
    payload: bytes = field(default=b"")

    @property
    def pkt_name(self) -> str:
        return PKT_NAMES.get(self.pkt_type, f"0x{self.pkt_type:02X}")


# ────────────────────────────────────────────
# Builder
# ────────────────────────────────────────────
def build_frame(pkt_type: int, seq: int, payload: bytes = b"") -> bytes:
    """Encode one VK frame ready to send over TCP."""
    payload_len = len(payload)
    if payload_len > MAX_PAYLOAD_SIZE:
        raise ValueError(f"payload too large: {payload_len}")
    # 9-byte header: magic(2) + version(1) + pkt_type(1) + payload_len(4 LE) + seq_lo(1)
    header = bytes([
        MAGIC_0, MAGIC_1, VK_VERSION, pkt_type & 0xFF,
        payload_len & 0xFF, (payload_len >> 8) & 0xFF,
        (payload_len >> 16) & 0xFF, (payload_len >> 24) & 0xFF,
        seq & 0xFF,
    ])
    frame_body = header + payload
    crc = crc16_ccitt(frame_body)
    return frame_body + struct.pack("<H", crc)


# ────────────────────────────────────────────
# Parser  (operates on a bytearray ring buffer)
# ────────────────────────────────────────────
ParseResult = tuple[Optional[VKFrame], int]  # (frame | None, bytes_consumed)


def try_parse_frame(buf: bytearray) -> ParseResult:
    """
    Try to extract one complete, valid frame from buf.

    Returns (VKFrame, bytes_consumed) on success.
    Returns (None, 0) when buf has too few bytes (need more data).
    Returns (None, 1) on protocol error — caller should advance 1 byte to resync.
    """
    if len(buf) < MIN_FRAME_SIZE:
        return None, 0

    # Magic bytes check
    if buf[0] != MAGIC_0 or buf[1] != MAGIC_1:
        return None, 1  # resync

    if len(buf) < HEADER_SIZE:
        return None, 0

    version  = buf[2]
    pkt_type = buf[3]
    payload_len = struct.unpack_from("<I", buf, 4)[0]  # uint32 LE at offset 4
    seq_lo   = buf[8]

    if version != VK_VERSION:
        return None, 1  # resync

    if payload_len > MAX_PAYLOAD_SIZE:
        return None, 1  # resync

    total_len = HEADER_SIZE + payload_len + CRC_SIZE
    if len(buf) < total_len:
        return None, 0  # need more data

    # CRC check: covers HEADER_SIZE + payload_len bytes
    body = bytes(buf[:HEADER_SIZE + payload_len])
    crc_expected = crc16_ccitt(body)
    crc_received = struct.unpack_from("<H", buf, HEADER_SIZE + payload_len)[0]

    if crc_expected != crc_received:
        return None, 1  # resync

    payload = bytes(buf[HEADER_SIZE: HEADER_SIZE + payload_len])
    frame = VKFrame(pkt_type=pkt_type, seq=seq_lo, payload=payload)
    return frame, total_len


# ────────────────────────────────────────────
# Payload struct helpers
# ────────────────────────────────────────────
def parse_handshake_hello(payload: bytes) -> dict:
    """Parse PKT_HANDSHAKE_HELLO payload."""
    if len(payload) < 64:
        payload = payload.ljust(64, b"\x00")
    device_id   = payload[0:32].rstrip(b"\x00").decode("ascii", errors="replace")
    ward_id     = payload[32:48].rstrip(b"\x00").decode("ascii", errors="replace")
    fw_version  = payload[48:64].rstrip(b"\x00").decode("ascii", errors="replace")
    capabilities = struct.unpack_from("<I", payload, 64)[0] if len(payload) >= 68 else 0
    return {
        "device_id": device_id,
        "ward_id": ward_id,
        "fw_version": fw_version,
        "capabilities": capabilities,
    }


def build_handshake_ack(seq: int, status: int = 0, token: str = "") -> bytes:
    """Build PKT_HANDSHAKE_ACK frame."""
    tok = token.encode("ascii")[:31].ljust(32, b"\x00")
    payload = bytes([status & 0xFF]) + tok
    return build_frame(Pkt.HANDSHAKE_ACK, seq, payload)


def build_pong(seq: int, uptime_ms: int = 0) -> bytes:
    """Build PKT_HEARTBEAT_PONG frame."""
    payload = struct.pack("<IbBBB", uptime_ms, 0, 0, 0, 0)
    return build_frame(Pkt.HEARTBEAT_PONG, seq, payload)


def parse_telemetry_env(payload: bytes) -> dict:
    """Parse PKT_TELEMETRY_ENV payload."""
    if len(payload) < 16:
        return {}
    temp, humidity, light_lux, noise_db, ts_ms = struct.unpack_from("<ffHHI", payload)
    return {
        "temperature_c": round(temp, 2),
        "humidity_pct": round(humidity, 2),
        "light_lux": light_lux,
        "noise_db": noise_db,
        "timestamp_ms": ts_ms,
    }


def parse_telemetry_oximeter(payload: bytes) -> dict:
    """Parse PKT_TELEMETRY_OXIMETER payload."""
    if len(payload) < 8:
        return {}
    bpm, spo2, finger, conf, ts_ms = struct.unpack_from("<BBBBI", payload)
    return {
        "bpm": bpm,
        "spo2_pct": spo2,
        "finger_detected": int(finger),
        "confidence": conf,
        "timestamp_ms": ts_ms,
    }


def parse_telemetry_system(payload: bytes) -> dict:
    """Parse PKT_TELEMETRY_SYSTEM payload."""
    if len(payload) < 8:
        return {}
    # uptime_ms(4) + rssi(1, signed) + flags(3 padding)
    uptime_ms = struct.unpack_from("<I", payload, 0)[0]
    rssi = struct.unpack_from("b", payload, 4)[0]  # signed int8
    free_heap = struct.unpack_from("<I", payload, 8)[0] if len(payload) >= 12 else 0
    return {
        "uptime_ms": uptime_ms,
        "rssi": rssi,
        "free_heap_bytes": free_heap,
    }


def build_event_ack(seq: int) -> bytes:
    """Build PKT_EVENT_ACK frame (server → firmware: operator confirmed the event)."""
    return build_frame(Pkt.EVENT_ACK, seq, b"")


def parse_event(payload: bytes) -> dict:
    """Parse PKT_EVENT_REQUEST / PKT_EVENT_EMERGENCY payload."""
    if len(payload) < 5:
        return {}
    timestamp_unix, trigger = struct.unpack_from("<IB", payload)
    return {
        "timestamp_unix": timestamp_unix,
        "trigger": trigger,  # 0=request, 1=emergency
    }


def parse_audio_session_start(payload: bytes) -> dict:
    if len(payload) < 10:
        return {}
    session_id, trigger, sample_rate, bits, codec, channels = struct.unpack_from("<IBHBBB", payload)
    return {
        "session_id": session_id,
        "trigger": trigger,
        "sample_rate_hz": sample_rate,
        "bits_per_sample": bits,
        "codec": codec,
        "channels": channels,
    }


def parse_audio_chunk_header(payload: bytes) -> tuple[dict, bytes]:
    """Returns (header_dict, adpcm_data)."""
    if len(payload) < 8:
        return {}, b""
    session_id, chunk_index, chunk_len = struct.unpack_from("<IHH", payload)
    adpcm_data = payload[8: 8 + chunk_len]
    return {
        "session_id": session_id,
        "chunk_index": chunk_index,
        "chunk_len": chunk_len,
    }, adpcm_data


def parse_audio_session_end(payload: bytes) -> dict:
    if len(payload) < 10:
        return {}
    session_id, total_chunks, total_pcm_samples = struct.unpack_from("<IHI", payload)
    return {
        "session_id": session_id,
        "total_chunks": total_chunks,
        "total_pcm_samples": total_pcm_samples,
    }
