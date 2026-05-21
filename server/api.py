"""
VitalKare MVP — FastAPI application + WebSocket endpoint.

REST routes:
  POST  /api/auth/login
  GET   /api/devices
  GET   /api/devices/{device_id}
  GET   /api/devices/{device_id}/audio
  GET   /api/events
  POST  /api/events/{event_id}/acknowledge
  GET   /api/admin/devices          (admin only)
  POST  /api/admin/devices/{id}/approve
  PATCH /api/admin/devices/{id}
  DELETE /api/admin/devices/{id}
  GET   /api/admin/events
  WS    /ws/dashboard

Auth: JWT Bearer (HS256). admin/admin → full access.
      No auth on GET /api/devices and WS for dashboard simplicity.
"""

from __future__ import annotations
import asyncio
import json
import logging
import os
import time
from datetime import datetime, timezone, timedelta
from typing import Any, Optional

from fastapi import Depends, FastAPI, HTTPException, WebSocket, WebSocketDisconnect, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from pydantic import BaseModel
from jose import jwt, JWTError

import db
from ws_manager import ws_manager

logger = logging.getLogger("vitalkare.api")

# ── JWT config ──────────────────────────────────────────────────
_JWT_SECRET  = os.environ.get("VK_JWT_SECRET", "vitalkare-mvp-secret-2024")
_JWT_ALG     = "HS256"
_JWT_EXPIRE  = timedelta(hours=12)

_bearer = HTTPBearer(auto_error=False)

app = FastAPI(title="VitalKare MVP", version="1.0.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ─────────────────────────────────────────────────────────────────
# JWT helpers
# ─────────────────────────────────────────────────────────────────
def _create_token(sub: str, role: str) -> str:
    expire = datetime.now(timezone.utc) + _JWT_EXPIRE
    payload = {"sub": sub, "role": role, "exp": expire}
    return jwt.encode(payload, _JWT_SECRET, algorithm=_JWT_ALG)


def _decode_token(token: str) -> dict:
    try:
        return jwt.decode(token, _JWT_SECRET, algorithms=[_JWT_ALG])
    except JWTError as exc:
        raise HTTPException(status_code=401, detail=f"Invalid token: {exc}") from exc


async def _get_current_user(
    creds: HTTPAuthorizationCredentials | None = Depends(_bearer),
) -> dict:
    if creds is None:
        raise HTTPException(status_code=401, detail="Not authenticated")
    return _decode_token(creds.credentials)


async def _require_admin(user: dict = Depends(_get_current_user)) -> dict:
    if user.get("role") != "admin":
        raise HTTPException(status_code=403, detail="Admin role required")
    return user


# ─────────────────────────────────────────────────────────────────
# Pydantic schemas
# ─────────────────────────────────────────────────────────────────
class LoginRequest(BaseModel):
    username: str
    password: str


class DevicePatch(BaseModel):
    patient_name: Optional[str] = None
    room_label: Optional[str] = None


class ProvisionedSlotCreate(BaseModel):
    bed_label: str
    expected_ip: str
    ward_id: str = ""
    patient_name: Optional[str] = None
    notes: Optional[str] = None


class ProvisionedSlotUpdate(BaseModel):
    bed_label: Optional[str] = None
    expected_ip: Optional[str] = None
    ward_id: Optional[str] = None
    patient_name: Optional[str] = None
    notes: Optional[str] = None


# ─────────────────────────────────────────────────────────────────
# Auth routes
# ─────────────────────────────────────────────────────────────────
@app.post("/api/auth/login")
async def login(req: LoginRequest) -> dict:
    operator = await db.verify_operator(req.username, req.password)
    if not operator:
        raise HTTPException(status_code=401, detail="Invalid credentials")
    token = _create_token(operator["username"], operator["role"])
    return {
        "access_token": token,
        "token_type": "bearer",
        "username": operator["username"],
        "role": operator["role"],
    }


# ─────────────────────────────────────────────────────────────────
# Device routes (public read)
# ─────────────────────────────────────────────────────────────────
@app.get("/api/devices")
async def list_devices() -> list[dict]:
    devices = await db.get_all_devices()
    # Only expose approved devices on public endpoint
    return [d for d in devices if d.get("approved")]


@app.get("/api/devices/{device_id}")
async def get_device(device_id: str) -> dict:
    device = await db.get_device(device_id)
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    return device


@app.get("/api/devices/{device_id}/audio")
async def list_audio(device_id: str) -> list[dict]:
    return await db.get_audio_sessions(device_id)


# ─────────────────────────────────────────────────────────────────
# Events
# ─────────────────────────────────────────────────────────────────
@app.get("/api/events")
async def list_events(device_id: Optional[str] = None, limit: int = 100) -> list[dict]:
    return await db.get_events(device_id=device_id, limit=limit)


@app.post("/api/events/{event_id}/acknowledge")
async def ack_event(event_id: int, _user: dict = Depends(_get_current_user)) -> dict:
    await db.acknowledge_event(event_id)
    await ws_manager.broadcast({
        "type": "event_acknowledged",
        "event_id": event_id,
        "timestamp": datetime.now(timezone.utc).isoformat(),
    })
    return {"ok": True}


# ─────────────────────────────────────────────────────────────────
# Admin routes
# ─────────────────────────────────────────────────────────────────
@app.get("/api/admin/devices")
async def admin_list_devices(_admin: dict = Depends(_require_admin)) -> list[dict]:
    """All devices — including unapproved."""
    return await db.get_all_devices()


@app.post("/api/admin/devices/{device_id}/approve")
async def admin_approve(device_id: str, _admin: dict = Depends(_require_admin)) -> dict:
    device = await db.get_device(device_id)
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    await db.approve_device(device_id)
    await ws_manager.broadcast({
        "type": "device_approved",
        "device_id": device_id,
        "timestamp": datetime.now(timezone.utc).isoformat(),
    })
    return {"ok": True}


@app.patch("/api/admin/devices/{device_id}")
async def admin_update_device(
    device_id: str, body: DevicePatch, _admin: dict = Depends(_require_admin)
) -> dict:
    device = await db.get_device(device_id)
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    await db.update_device_meta(device_id, body.patient_name, body.room_label)
    updated = await db.get_device(device_id)
    await ws_manager.broadcast({
        "type": "device_updated",
        "device_id": device_id,
        "patient_name": body.patient_name,
        "room_label": body.room_label,
        "timestamp": datetime.now(timezone.utc).isoformat(),
    })
    return updated or {}


@app.delete("/api/admin/devices/{device_id}")
async def admin_delete_device(device_id: str, _admin: dict = Depends(_require_admin)) -> dict:
    await db.delete_device(device_id)
    await ws_manager.broadcast({
        "type": "device_removed",
        "device_id": device_id,
        "timestamp": datetime.now(timezone.utc).isoformat(),
    })
    return {"ok": True}


@app.get("/api/admin/events")
async def admin_events(_admin: dict = Depends(_require_admin), limit: int = 200) -> list[dict]:
    return await db.get_events(limit=limit)


# ─────────────────────────────────────────────────────────────────
# Commissioning (provisioned slots)
# ─────────────────────────────────────────────────────────────────
@app.get("/api/admin/provisioned")
async def admin_list_provisioned(_admin: dict = Depends(_require_admin)) -> list[dict]:
    return await db.get_all_provisioned_slots()


@app.post("/api/admin/provisioned")
async def admin_create_provisioned(
    body: ProvisionedSlotCreate, _admin: dict = Depends(_require_admin)
) -> dict:
    return await db.create_provisioned_slot(
        body.bed_label, body.expected_ip, body.ward_id, body.patient_name, body.notes
    )


@app.put("/api/admin/provisioned/{slot_id}")
async def admin_update_provisioned(
    slot_id: int, body: ProvisionedSlotUpdate, _admin: dict = Depends(_require_admin)
) -> dict:
    slot = await db.update_provisioned_slot(
        slot_id,
        body.bed_label,
        body.expected_ip,
        body.ward_id,
        body.patient_name,
        body.notes,
    )
    if not slot:
        raise HTTPException(status_code=404, detail="Slot not found")
    return slot


@app.delete("/api/admin/provisioned/{slot_id}")
async def admin_delete_provisioned(
    slot_id: int, _admin: dict = Depends(_require_admin)
) -> dict:
    await db.delete_provisioned_slot(slot_id)
    return {"ok": True}


# ─────────────────────────────────────────────────────────────────
# WebSocket — real-time dashboard
# ─────────────────────────────────────────────────────────────────
@app.websocket("/ws/dashboard")
async def ws_dashboard(websocket: WebSocket) -> None:
    await websocket.accept()

    # Optional auth: client may send {"type":"auth","token":"<jwt>"}
    # If no auth message within 2 s, connect anyway (MVP: read-only feed)
    token_payload: dict = {}
    try:
        raw = await asyncio.wait_for(websocket.receive_text(), timeout=2.0)
        msg = json.loads(raw)
        if msg.get("type") == "auth":
            try:
                token_payload = _decode_token(msg["token"])
            except HTTPException:
                pass  # unauthenticated WS is still allowed to receive
    except Exception:
        pass  # timeout or non-JSON — proceed unauthenticated

    ws_manager.register(websocket)

    # Push current device snapshot — DB calls are outside the client send try/except,
    # so wrap them separately to avoid ECONNRESET when the DB is missing a table.
    try:
        devices = await db.get_all_devices()
        provisioned = await db.get_all_provisioned_slots()
        await websocket.send_text(json.dumps({
            "type": "snapshot",
            "devices": devices,
            "events": await db.get_events(limit=50),
            "provisioned": provisioned,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }, default=str))
    except Exception as exc:
        logger.warning("ws_dashboard: failed to send initial snapshot (%s) — closing", exc)
        ws_manager.unregister(websocket)
        return

    try:
        while True:
            # Keep-alive: wait for client messages (acknowledge, etc.)
            data = await websocket.receive_text()
            try:
                msg = json.loads(data)
                if msg.get("type") == "acknowledge" and "event_id" in msg:
                    await db.acknowledge_event(int(msg["event_id"]))
                    await ws_manager.broadcast({
                        "type": "event_acknowledged",
                        "event_id": msg["event_id"],
                        "timestamp": datetime.now(timezone.utc).isoformat(),
                    })
            except Exception:
                pass
    except WebSocketDisconnect:
        pass
    except Exception as exc:
        logger.warning("ws_dashboard: connection error (%s)", exc)
    finally:
        ws_manager.unregister(websocket)


# ─────────────────────────────────────────────────────────────────
# Health check
# ─────────────────────────────────────────────────────────────────
@app.get("/health")
async def health() -> dict:
    return {"status": "ok", "time": datetime.now(timezone.utc).isoformat()}
