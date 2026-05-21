"""
Active TCP connection registry — maps device_id to its live send callable.

Used by REST/WebSocket handlers to push server-initiated frames (e.g., EVENT_ACK)
to a connected firmware device without coupling api.py to tcp_handler internals.
"""

from __future__ import annotations
import logging
from typing import Callable, Optional

logger = logging.getLogger("vitalkare.connections")

# device_id → (send_fn, next_seq_fn)
_registry: dict[str, tuple[Callable, Callable]] = {}


def register(device_id: str, send_fn: Callable, next_seq_fn: Callable) -> None:
    if device_id in _registry:
        logger.debug("Connection for %s replaced (reconnect)", device_id)
    _registry[device_id] = (send_fn, next_seq_fn)
    logger.debug("Registered TCP connection for %s", device_id)


def unregister(device_id: str) -> None:
    if _registry.pop(device_id, None) is not None:
        logger.debug("Unregistered TCP connection for %s", device_id)


def is_connected(device_id: str) -> bool:
    return device_id in _registry


async def send_frame(device_id: str, frame_bytes: bytes) -> bool:
    """Send a pre-built VK frame to a connected device. Returns True on success."""
    entry = _registry.get(device_id)
    if not entry:
        logger.debug("send_frame: no active connection for %s", device_id)
        return False
    send_fn, _ = entry
    try:
        await send_fn(frame_bytes)
        return True
    except Exception as exc:
        logger.warning("send_frame to %s failed: %s", device_id, exc)
        return False


def next_seq(device_id: str) -> Optional[int]:
    """Return the next TX sequence number for the given device, or None if disconnected."""
    entry = _registry.get(device_id)
    if not entry:
        return None
    _, next_seq_fn = entry
    return next_seq_fn()
