"""
WebSocket broadcast manager — keeps a set of active dashboard connections.
Thread-safe for asyncio single-thread use.
"""

from __future__ import annotations
import json
import logging
from typing import Any

from fastapi import WebSocket

logger = logging.getLogger("vitalkare.ws")


class WSManager:
    def __init__(self) -> None:
        self._connections: set[WebSocket] = set()

    def register(self, ws: WebSocket) -> None:
        self._connections.add(ws)
        logger.debug("WS connected. Total: %d", len(self._connections))

    def unregister(self, ws: WebSocket) -> None:
        self._connections.discard(ws)
        logger.debug("WS disconnected. Total: %d", len(self._connections))

    async def broadcast(self, message: dict[str, Any]) -> None:
        if not self._connections:
            return
        payload = json.dumps(message, default=str, ensure_ascii=False)
        dead: list[WebSocket] = []
        for ws in list(self._connections):
            try:
                await ws.send_text(payload)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.unregister(ws)

    async def send(self, ws: WebSocket, message: dict[str, Any]) -> None:
        try:
            await ws.send_text(json.dumps(message, default=str, ensure_ascii=False))
        except Exception:
            self.unregister(ws)


# Singleton used by tcp_handler.py and api.py
ws_manager = WSManager()
