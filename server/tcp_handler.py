"""
TCP connection handler — one coroutine per connected ESP32 device.

Flow for each connection:
  1. Read VK frames from the TCP stream.
  2. Route by pkt_type: handshake → register; telemetry → update DB + broadcast WS;
     event → store + broadcast; audio → write to file.
  3. Send response frames back (ACK, PONG) on the same connection.
  4. On disconnect: mark device OFFLINE, broadcast.

Audio files are stored under:
  server/audio/YYYY-MM-DD/<device_id>/session_<id>.adpcm
"""

from __future__ import annotations
import asyncio
import logging
import os
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import vk_proto as vk
import db
from ws_manager import ws_manager

logger = logging.getLogger("vitalkare.tcp")

AUDIO_DIR = Path(__file__).parent / "audio"
RECV_BUF  = 4096
TCP_TIMEOUT_S = 90  # disconnect if no frame received in this many seconds


# ─────────────────────────────────────────────────────────────────
# Audio session state (per device, in-memory while recording)
# ─────────────────────────────────────────────────────────────────
class AudioSession:
    def __init__(self, session_id: int, device_id: str, trigger: int) -> None:
        self.session_id = session_id
        self.device_id  = device_id
        self.trigger    = trigger
        self.chunk_count = 0
        date_str = datetime.now(timezone.utc).strftime("%Y-%m-%d")
        out_dir = AUDIO_DIR / date_str / device_id
        out_dir.mkdir(parents=True, exist_ok=True)
        self.file_path = out_dir / f"session_{session_id}.adpcm"
        self._fp = open(self.file_path, "wb")
        logger.info("Audio session %d started → %s", session_id, self.file_path)

    def write_chunk(self, data: bytes) -> None:
        self._fp.write(data)
        self._fp.flush()
        self.chunk_count += 1

    def close(self) -> str:
        self._fp.close()
        return str(self.file_path)


# ─────────────────────────────────────────────────────────────────
# Per-connection handler
# ─────────────────────────────────────────────────────────────────
async def handle_connection(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
) -> None:
    peer = writer.get_extra_info("peername", ("?", 0))
    logger.info("New TCP connection from %s:%d", peer[0], peer[1])

    buf = bytearray()
    device_id: Optional[str] = None
    seq_tx: int = 0
    audio_sessions: dict[int, AudioSession] = {}

    async def send(frame_bytes: bytes) -> None:
        try:
            writer.write(frame_bytes)
            await writer.drain()
        except Exception as exc:
            logger.warning("Send error to %s: %s", device_id or peer[0], exc)

    def next_seq() -> int:
        nonlocal seq_tx
        seq_tx = (seq_tx + 1) & 0xFF  # 8-bit (matches firmware's 1-byte seq)
        return seq_tx

    try:
        while True:
            # Read with timeout — detect dead connections
            try:
                data = await asyncio.wait_for(reader.read(RECV_BUF), timeout=TCP_TIMEOUT_S)
            except asyncio.TimeoutError:
                logger.warning("Timeout waiting for data from %s", device_id or peer[0])
                break

            if not data:
                logger.info("Device %s closed connection", device_id or peer[0])
                break

            buf.extend(data)

            # Parse all complete frames from buffer
            while True:
                frame, consumed = vk.try_parse_frame(buf)
                if consumed == 0:
                    break  # need more data
                if consumed == 1 and frame is None:
                    # Resync: skip 1 byte
                    logger.debug("Resync (bad byte 0x%02X) from %s", buf[0], device_id or peer[0])
                    del buf[0]
                    continue
                # consumed > 1 and frame is valid
                del buf[:consumed]
                await _dispatch(frame, device_id, send, next_seq, audio_sessions, peer)
                if frame.pkt_type == vk.Pkt.HANDSHAKE_HELLO:
                    # Update device_id after handshake
                    parsed = vk.parse_handshake_hello(frame.payload)
                    device_id = parsed.get("device_id") or device_id

    except ConnectionResetError:
        logger.info("Device %s reset connection", device_id or peer[0])
    except Exception as exc:
        logger.exception("Unhandled error in TCP handler for %s: %s", device_id or peer[0], exc)
    finally:
        writer.close()
        # Close any open audio sessions
        for sess in audio_sessions.values():
            path = sess.close()
            await db.audio_session_end(sess.session_id, sess.chunk_count, path)
        # Mark device OFFLINE
        if device_id:
            await db.update_device_status(device_id, "OFFLINE")
            await ws_manager.broadcast({
                "type": "device_status",
                "device_id": device_id,
                "status": "OFFLINE",
                "timestamp": datetime.now(timezone.utc).isoformat(),
            })
        logger.info("Connection closed for %s", device_id or peer[0])


# ─────────────────────────────────────────────────────────────────
# Frame dispatcher
# ─────────────────────────────────────────────────────────────────
async def _dispatch(
    frame: vk.VKFrame,
    device_id: Optional[str],
    send,
    next_seq,
    audio_sessions: dict,
    peer: tuple,
) -> None:
    t = frame.pkt_type

    # ── Handshake ──────────────────────────────────────────
    if t == vk.Pkt.HANDSHAKE_HELLO:
        info = vk.parse_handshake_hello(frame.payload)
        dev_id = info.get("device_id", "")
        logger.info("HANDSHAKE_HELLO from device_id=%r ward=%r fw=%s",
                    dev_id, info.get("ward_id"), info.get("fw_version"))
        # Register in DB (approved defaults to 0 = pending)
        await db.upsert_device(
            dev_id,
            info.get("ward_id", ""),
            info.get("fw_version", ""),
            info.get("capabilities", 0),
        )

        # ── Auto-commissioning: check if connecting IP matches a provisioned slot ──
        peer_ip = peer[0]
        slot = await db.find_slot_by_ip(peer_ip)
        if slot:
            await db.approve_device(dev_id)
            await db.update_device_meta(
                dev_id,
                slot.get("patient_name"),
                slot.get("bed_label"),
            )
            await db.link_slot_to_device(slot["id"], dev_id)
            logger.info(
                "Device %s auto-commissioned via slot id=%d (IP: %s, bed: %s)",
                dev_id, slot["id"], peer_ip, slot["bed_label"],
            )

        # Approve immediately if already approved (reconnect or just commissioned)
        device = await db.get_device(dev_id)
        if device and device.get("approved"):
            await db.update_device_status(dev_id, "ONLINE")
        # Send ACK
        ack = vk.build_handshake_ack(next_seq(), status=0, token="")
        await send(ack)
        # Broadcast new/reconnected device to dashboard
        updated = await db.get_device(dev_id)
        await ws_manager.broadcast({
            "type": "device_status",
            "device_id": dev_id,
            "status": "ONLINE" if (updated and updated.get("approved")) else "PENDING",
            "ward_id": info.get("ward_id", ""),
            "fw_version": info.get("fw_version", ""),
            "patient_name": updated.get("patient_name") if updated else None,
            "room_label": updated.get("room_label") if updated else None,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })
        return

    if device_id is None:
        logger.warning("Packet 0x%02X received before handshake from %s", t, peer[0])
        return

    # ── Heartbeat ──────────────────────────────────────────
    if t == vk.Pkt.HEARTBEAT_PING:
        pong = vk.build_pong(next_seq())
        await send(pong)
        await db.update_device_status(device_id, "ONLINE")
        return

    # ── Telemetry ──────────────────────────────────────────
    if t == vk.Pkt.TELEMETRY_ENV:
        data = vk.parse_telemetry_env(frame.payload)
        await db.update_device_telemetry(device_id, data)
        await ws_manager.broadcast({
            "type": "telemetry",
            "device_id": device_id,
            **data,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })
        return

    if t == vk.Pkt.TELEMETRY_OXIMETER:
        data = vk.parse_telemetry_oximeter(frame.payload)
        await db.update_device_telemetry(device_id, data)
        await ws_manager.broadcast({
            "type": "telemetry",
            "device_id": device_id,
            **data,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })
        return

    if t == vk.Pkt.TELEMETRY_SYSTEM:
        data = vk.parse_telemetry_system(frame.payload)
        await db.update_device_telemetry(device_id, data)
        await ws_manager.broadcast({
            "type": "telemetry",
            "device_id": device_id,
            **data,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })
        return

    # ── Events ─────────────────────────────────────────────
    if t in (vk.Pkt.EVENT_REQUEST, vk.Pkt.EVENT_EMERGENCY):
        data = vk.parse_event(frame.payload)
        event_type = "EMERGENCY" if t == vk.Pkt.EVENT_EMERGENCY else "REQUEST"
        event_id = await db.insert_event(
            device_id, event_type, data.get("timestamp_unix") or time.time()
        )
        if t == vk.Pkt.EVENT_EMERGENCY:
            await db.update_device_status(device_id, "EMERGENCY")
        logger.info("EVENT %s from %s (id=%d)", event_type, device_id, event_id)
        await ws_manager.broadcast({
            "type": "event",
            "event_id": event_id,
            "device_id": device_id,
            "event_type": event_type,
            "timestamp": datetime.fromtimestamp(
                data.get("timestamp_unix", time.time()), tz=timezone.utc
            ).isoformat(),
        })
        return

    # ── Audio ──────────────────────────────────────────────
    if t == vk.Pkt.AUDIO_SESSION_START:
        info = vk.parse_audio_session_start(frame.payload)
        sid  = info.get("session_id", 0)
        trig = info.get("trigger", 0)
        await db.audio_session_begin(sid, device_id, trig)
        sess = AudioSession(sid, device_id, trig)
        audio_sessions[sid] = sess
        await ws_manager.broadcast({
            "type": "audio_session_start",
            "device_id": device_id,
            "session_id": sid,
            "trigger": trig,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })
        return

    if t == vk.Pkt.AUDIO_CHUNK:
        hdr, adpcm_data = vk.parse_audio_chunk_header(frame.payload)
        sid = hdr.get("session_id", 0)
        if sid in audio_sessions and adpcm_data:
            audio_sessions[sid].write_chunk(adpcm_data)
        return

    if t == vk.Pkt.AUDIO_SESSION_END:
        info = vk.parse_audio_session_end(frame.payload)
        sid  = info.get("session_id", 0)
        if sid in audio_sessions:
            sess = audio_sessions.pop(sid)
            path = sess.close()
            await db.audio_session_end(sid, info.get("total_chunks", sess.chunk_count), path)
            await ws_manager.broadcast({
                "type": "audio_session_end",
                "device_id": device_id,
                "session_id": sid,
                "total_chunks": info.get("total_chunks"),
                "file_path": str(path),
                "timestamp": datetime.now(timezone.utc).isoformat(),
            })
        return

    logger.debug("Unhandled pkt 0x%02X (%s) from %s", t, frame.pkt_name, device_id)
