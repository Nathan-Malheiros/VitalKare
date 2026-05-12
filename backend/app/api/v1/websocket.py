"""
VitalKare — Endpoint WebSocket (/ws/dashboard)

Clientes conectados recebem broadcast em tempo real de:
  - Emergências
  - Mudanças de status de leitos
  - Dados ambientais
  - Confirmações de atendimento
"""

import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from jose import JWTError

from app.core.notification import NotificationService
from app.utils.security import decode_token

router = APIRouter(tags=["WebSocket"])
logger = logging.getLogger("vitalkare.ws")

notification = NotificationService()


@router.websocket("/dashboard")
async def ws_dashboard(ws: WebSocket):
    """
    Conexão WebSocket autenticada.

    O cliente deve enviar um token JWT logo após conectar:
        {"type": "auth", "token": "<JWT>"}

    Após autenticação bem-sucedida recebe mensagens de broadcast.
    """
    await ws.accept()

    # Aguarda autenticação (primeira mensagem)
    try:
        auth_msg = await ws.receive_json()
        token = auth_msg.get("token", "")
        decode_token(token)  # lança JWTError se inválido
    except (JWTError, Exception) as exc:
        logger.warning("WebSocket: autenticação falhou — %s", exc)
        await ws.send_json({"type": "error", "detail": "Autenticação inválida"})
        await ws.close(code=4001)
        return

    notification.register(ws)
    await ws.send_json({"type": "connected", "detail": "Conectado ao VitalKare Dashboard"})
    logger.info("WebSocket conectado. Clientes ativos: %d",
                len(notification._connections))  # noqa: SLF001

    try:
        # Mantém conexão aberta aguardando mensagens do cliente
        # (ex: ping/pong ou solicitação de ack)
        while True:
            data = await ws.receive_json()
            if data.get("type") == "ping":
                await ws.send_json({"type": "pong"})
    except WebSocketDisconnect:
        pass
    finally:
        notification.unregister(ws)
        logger.info("WebSocket desconectado. Clientes ativos: %d",
                    len(notification._connections))  # noqa: SLF001
