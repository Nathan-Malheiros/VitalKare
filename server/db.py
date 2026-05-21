"""
SQLite database — VitalKare MVP

Tables:
  devices       — registered ESP32 devices + live telemetry snapshot
  events        — button presses and emergencies
  audio_sessions — audio recording sessions
  operators     — dashboard users (admin/admin by default)
"""

from __future__ import annotations
import hashlib
import os
import time
from datetime import datetime, timezone
from pathlib import Path

import aiosqlite

DB_PATH = Path(__file__).parent / "vitalkare_mvp.db"


# ────────────────────────────────────────────
# Init / migrations
# ────────────────────────────────────────────
_SCHEMA = """
CREATE TABLE IF NOT EXISTS devices (
    device_id        TEXT PRIMARY KEY,
    ward_id          TEXT NOT NULL DEFAULT '',
    fw_version       TEXT NOT NULL DEFAULT '',
    capabilities     INTEGER NOT NULL DEFAULT 0,
    approved         INTEGER NOT NULL DEFAULT 0,  -- 0=pending, 1=approved
    patient_name     TEXT,
    room_label       TEXT,
    status           TEXT NOT NULL DEFAULT 'OFFLINE',
    last_seen        REAL,         -- unix timestamp
    rssi             INTEGER,
    free_heap_bytes  INTEGER,
    uptime_ms        INTEGER,
    bpm              INTEGER,
    spo2_pct         INTEGER,
    finger_detected  INTEGER,
    temperature_c    REAL,
    humidity_pct     REAL,
    light_lux        INTEGER,
    noise_db         INTEGER,
    created_at       REAL NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id   TEXT NOT NULL,
    event_type  TEXT NOT NULL,   -- 'REQUEST' or 'EMERGENCY'
    timestamp   REAL NOT NULL,   -- unix timestamp
    acknowledged INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS audio_sessions (
    session_id   INTEGER PRIMARY KEY,
    device_id    TEXT NOT NULL,
    trigger      INTEGER NOT NULL DEFAULT 0,   -- 0=request, 1=emergency
    start_time   REAL,
    end_time     REAL,
    total_chunks INTEGER,
    file_path    TEXT,
    status       TEXT NOT NULL DEFAULT 'RECORDING'  -- RECORDING|COMPLETE|ERROR
);

CREATE TABLE IF NOT EXISTS operators (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    username     TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    role         TEXT NOT NULL DEFAULT 'operator'
);

CREATE TABLE IF NOT EXISTS provisioned_slots (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    bed_label    TEXT NOT NULL,
    expected_ip  TEXT NOT NULL,
    ward_id      TEXT NOT NULL DEFAULT '',
    patient_name TEXT,
    notes        TEXT,
    device_id    TEXT,
    created_at   REAL NOT NULL DEFAULT (unixepoch())
);
"""

_DEFAULT_ADMIN_PASS = "admin"


def _hash_password(password: str) -> str:
    """Simple SHA-256 hash for MVP (no bcrypt dependency at startup time)."""
    return hashlib.sha256(password.encode()).hexdigest()


async def init_db() -> None:
    """Create tables and seed admin user."""
    async with aiosqlite.connect(DB_PATH) as db:
        await db.executescript(_SCHEMA)
        # Seed admin/admin if not present
        existing = await db.execute(
            "SELECT id FROM operators WHERE username = ?", ("admin",)
        )
        row = await existing.fetchone()
        if not row:
            await db.execute(
                "INSERT INTO operators (username, password_hash, role) VALUES (?, ?, ?)",
                ("admin", _hash_password(_DEFAULT_ADMIN_PASS), "admin"),
            )
        await db.commit()


# ────────────────────────────────────────────
# Device helpers
# ────────────────────────────────────────────
async def upsert_device(device_id: str, ward_id: str, fw_version: str, capabilities: int) -> dict:
    """Register or update a device on handshake. Returns the device row."""
    now = time.time()
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        await db.execute(
            """
            INSERT INTO devices (device_id, ward_id, fw_version, capabilities, last_seen, created_at)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
                ward_id      = excluded.ward_id,
                fw_version   = excluded.fw_version,
                capabilities = excluded.capabilities,
                last_seen    = excluded.last_seen
            """,
            (device_id, ward_id, fw_version, capabilities, now, now),
        )
        await db.commit()
        cursor = await db.execute("SELECT * FROM devices WHERE device_id = ?", (device_id,))
        row = await cursor.fetchone()
        return dict(row) if row else {}


async def update_device_status(device_id: str, status: str) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE devices SET status = ?, last_seen = ? WHERE device_id = ?",
            (status, time.time(), device_id),
        )
        await db.commit()


async def update_device_heartbeat(device_id: str) -> None:
    """Update last_seen and set ONLINE — but preserve EMERGENCY status."""
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            """
            UPDATE devices SET
                last_seen = ?,
                status    = CASE WHEN status = 'EMERGENCY' THEN 'EMERGENCY' ELSE 'ONLINE' END
            WHERE device_id = ?
            """,
            (time.time(), device_id),
        )
        await db.commit()


async def update_device_telemetry(device_id: str, telemetry: dict) -> None:
    allowed = {
        "rssi", "free_heap_bytes", "uptime_ms",
        "bpm", "spo2_pct", "finger_detected",
        "temperature_c", "humidity_pct", "light_lux", "noise_db",
    }
    updates = {k: v for k, v in telemetry.items() if k in allowed}
    if not updates:
        return
    updates["last_seen"] = time.time()
    cols = ", ".join(f"{k} = ?" for k in updates)
    vals = list(updates.values()) + [device_id]
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(f"UPDATE devices SET {cols} WHERE device_id = ?", vals)
        await db.commit()


async def get_all_devices() -> list[dict]:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute("SELECT * FROM devices ORDER BY created_at")
        rows = await cursor.fetchall()
        return [dict(r) for r in rows]


async def get_device(device_id: str) -> dict | None:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute("SELECT * FROM devices WHERE device_id = ?", (device_id,))
        row = await cursor.fetchone()
        return dict(row) if row else None


async def approve_device(device_id: str) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE devices SET approved = 1, status = 'OFFLINE' WHERE device_id = ?",
            (device_id,),
        )
        await db.commit()


async def update_device_meta(device_id: str, patient_name: str | None, room_label: str | None) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE devices SET patient_name = ?, room_label = ? WHERE device_id = ?",
            (patient_name, room_label, device_id),
        )
        await db.commit()


async def delete_device(device_id: str) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("DELETE FROM devices WHERE device_id = ?", (device_id,))
        await db.commit()


# ────────────────────────────────────────────
# Events
# ────────────────────────────────────────────
async def insert_event(device_id: str, event_type: str, timestamp: float | None = None) -> int:
    ts = timestamp or time.time()
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            "INSERT INTO events (device_id, event_type, timestamp) VALUES (?, ?, ?)",
            (device_id, event_type, ts),
        )
        await db.commit()
        return cursor.lastrowid or 0


async def acknowledge_event(event_id: int) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("UPDATE events SET acknowledged = 1 WHERE id = ?", (event_id,))
        await db.commit()


async def get_event(event_id: int) -> dict | None:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute("SELECT * FROM events WHERE id = ?", (event_id,))
        row = await cursor.fetchone()
        return dict(row) if row else None


async def update_device_last_seen(device_id: str) -> None:
    """Update only last_seen timestamp without changing status."""
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE devices SET last_seen = ? WHERE device_id = ?",
            (time.time(), device_id),
        )
        await db.commit()


async def get_events(device_id: str | None = None, limit: int = 100) -> list[dict]:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        if device_id:
            cursor = await db.execute(
                "SELECT * FROM events WHERE device_id = ? ORDER BY timestamp DESC LIMIT ?",
                (device_id, limit),
            )
        else:
            cursor = await db.execute(
                "SELECT * FROM events ORDER BY timestamp DESC LIMIT ?", (limit,)
            )
        rows = await cursor.fetchall()
        return [dict(r) for r in rows]


# ────────────────────────────────────────────
# Audio sessions
# ────────────────────────────────────────────
async def audio_session_begin(session_id: int, device_id: str, trigger: int) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            """
            INSERT OR IGNORE INTO audio_sessions (session_id, device_id, trigger, start_time)
            VALUES (?, ?, ?, ?)
            """,
            (session_id, device_id, trigger, time.time()),
        )
        await db.commit()


async def audio_session_end(session_id: int, total_chunks: int, file_path: str) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            """
            UPDATE audio_sessions
            SET end_time = ?, total_chunks = ?, file_path = ?, status = 'COMPLETE'
            WHERE session_id = ?
            """,
            (time.time(), total_chunks, file_path, session_id),
        )
        await db.commit()


async def get_audio_sessions(device_id: str, limit: int = 50) -> list[dict]:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM audio_sessions WHERE device_id = ? ORDER BY start_time DESC LIMIT ?",
            (device_id, limit),
        )
        rows = await cursor.fetchall()
        return [dict(r) for r in rows]


# ────────────────────────────────────────────
# Auth
# ────────────────────────────────────────────
async def verify_operator(username: str, password: str) -> dict | None:
    """Return operator dict if credentials are valid, else None."""
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM operators WHERE username = ?", (username,)
        )
        row = await cursor.fetchone()
        if not row:
            return None
        if row["password_hash"] == _hash_password(password):
            return dict(row)
        return None


# ────────────────────────────────────────────
# Provisioned slots (commissioning)
# ────────────────────────────────────────────
async def get_all_provisioned_slots() -> list[dict]:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM provisioned_slots ORDER BY created_at"
        )
        rows = await cursor.fetchall()
        return [dict(r) for r in rows]


async def find_slot_by_ip(ip: str) -> dict | None:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM provisioned_slots WHERE expected_ip = ?", (ip,)
        )
        row = await cursor.fetchone()
        return dict(row) if row else None


async def create_provisioned_slot(
    bed_label: str,
    expected_ip: str,
    ward_id: str = "",
    patient_name: str | None = None,
    notes: str | None = None,
) -> dict:
    now = time.time()
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            """
            INSERT INTO provisioned_slots (bed_label, expected_ip, ward_id, patient_name, notes, created_at)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (bed_label, expected_ip, ward_id, patient_name, notes, now),
        )
        await db.commit()
        row = await (await db.execute(
            "SELECT * FROM provisioned_slots WHERE id = ?", (cursor.lastrowid,)
        )).fetchone()
        return dict(row) if row else {}


async def update_provisioned_slot(
    slot_id: int,
    bed_label: str | None = None,
    expected_ip: str | None = None,
    ward_id: str | None = None,
    patient_name: str | None = None,
    notes: str | None = None,
) -> dict:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        existing = await (await db.execute(
            "SELECT * FROM provisioned_slots WHERE id = ?", (slot_id,)
        )).fetchone()
        if not existing:
            return {}
        new_bed_label    = bed_label    if bed_label    is not None else existing["bed_label"]
        new_ip           = expected_ip  if expected_ip  is not None else existing["expected_ip"]
        new_ward         = ward_id      if ward_id      is not None else existing["ward_id"]
        new_patient      = patient_name if patient_name is not None else existing["patient_name"]
        new_notes        = notes        if notes        is not None else existing["notes"]
        await db.execute(
            """
            UPDATE provisioned_slots
            SET bed_label=?, expected_ip=?, ward_id=?, patient_name=?, notes=?
            WHERE id=?
            """,
            (new_bed_label, new_ip, new_ward, new_patient, new_notes, slot_id),
        )
        await db.commit()
        row = await (await db.execute(
            "SELECT * FROM provisioned_slots WHERE id = ?", (slot_id,)
        )).fetchone()
        return dict(row) if row else {}


async def delete_provisioned_slot(slot_id: int) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("DELETE FROM provisioned_slots WHERE id = ?", (slot_id,))
        await db.commit()


async def link_slot_to_device(slot_id: int, device_id: str) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE provisioned_slots SET device_id = ? WHERE id = ?",
            (device_id, slot_id),
        )
        await db.commit()
