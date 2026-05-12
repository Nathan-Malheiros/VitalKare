"""
VitalKare — Serviço de notificação WebSocket

Mantém o registro de conexões ativas e faz broadcast de mensagens
para todos os clientes conectados (enfermeiros / médicos no dashboard).
"""

import asyncio
import json
import logging
from typing import Any
from weakref import WeakSet

from fastapi import WebSocket

logger = logging.getLogger("vitalkare.notification")


class NotificationService:
    """Singleton de broadcast WebSocket."""

    _instance: "NotificationService | None" = None
    _connections: WeakSet[WebSocket]

    def __new__(cls):
        if cls._instance is None:
            obj = super().__new__(cls)
            obj._connections = WeakSet()
            cls._instance = obj
        return cls._instance

    def register(self, ws: WebSocket) -> None:
        self._connections.add(ws)
        logger.debug("WebSocket registrado. Conexões ativas: %d", len(self._connections))

    def unregister(self, ws: WebSocket) -> None:
        self._connections.discard(ws)
        logger.debug("WebSocket removido. Conexões ativas: %d", len(self._connections))

    async def broadcast(self, message: dict[str, Any]) -> None:
        """Envia mensagem JSON para todos os clientes conectados."""
        if not self._connections:
            return

        payload = json.dumps(message, default=str, ensure_ascii=False)
        dead: list[WebSocket] = []

        for ws in set(self._connections):
            try:
                await ws.send_text(payload)
            except Exception:
                dead.append(ws)

        for ws in dead:
            self.unregister(ws)

    async def send_to(self, ws: WebSocket, message: dict[str, Any]) -> None:
        """Envia mensagem para um cliente específico."""
        try:
            await ws.send_text(json.dumps(message, default=str))
        except Exception as exc:
            logger.warning("Erro ao enviar para cliente: %s", exc)
            self.unregister(ws)
