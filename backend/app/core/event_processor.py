"""
VitalKare — Processador de eventos MQTT recebidos

Recebe payloads validados do mqtt_handler e executa:
  - Persistência no banco de dados
  - Notificação via WebSocket para o dashboard
  - Escalação de alertas por prioridade
"""

import logging
from datetime import datetime, timezone
from typing import Any

from app.db.session import AsyncSessionLocal
from app.models.bed import Bed, BedStatus
from app.models.event import Event, EventType, EventStatus
from app.core.notification import NotificationService

logger = logging.getLogger("vitalkare.processor")


class EventProcessor:
    """Processa mensagens MQTT e persiste no banco de dados."""

    def __init__(self):
        self._notification = NotificationService()

    # =========================================================
    # STATUS / HEARTBEAT
    # =========================================================
    async def process_status(self, topic: str, payload: dict[str, Any]) -> None:
        device_id = payload.get("device_id")
        if not device_id:
            return

        async with AsyncSessionLocal() as session:
            bed = await self._get_bed_by_device_id(session, device_id)
            if not bed:
                logger.warning("Heartbeat de dispositivo desconhecido: %s", device_id)
                return

            bed.last_heartbeat = datetime.now(timezone.utc)
            bed.wifi_rssi = payload.get("wifi_rssi")
            bed.free_heap_bytes = payload.get("free_heap_bytes")
            bed.firmware_version = payload.get("firmware_version")

            if payload.get("online", True) and bed.status == BedStatus.OFFLINE:
                bed.status = BedStatus.ONLINE
                await self._notification.broadcast({
                    "type": "bed_status",
                    "bed_id": bed.bed_number,
                    "status": "ONLINE",
                })

            await session.commit()

    # =========================================================
    # EMERGÊNCIA — P1
    # =========================================================
    async def process_emergency(self, topic: str, payload: dict[str, Any]) -> None:
        device_id = payload.get("device_id")
        logger.critical("EMERGÊNCIA recebida de %s", device_id)

        async with AsyncSessionLocal() as session:
            bed = await self._get_bed_by_device_id(session, device_id)
            if not bed:
                return

            bed.status = BedStatus.EMERGENCY

            event = Event(
                bed_id=bed.id,
                event_type=EventType.EMERGENCY,
                priority=1,
                status=EventStatus.PENDING,
                payload=payload,
                source_topic=topic,
            )
            session.add(event)
            await session.commit()
            await session.refresh(event)

        # Notifica todos os clientes WebSocket conectados
        await self._notification.broadcast({
            "type": "emergency",
            "event_id": str(event.id),
            "bed_id": bed.bed_number,
            "ward": payload.get("ward"),
            "device_id": device_id,
            "timestamp": payload.get("timestamp"),
        })

    # =========================================================
    # SOLICITAÇÃO (áudio / texto)
    # =========================================================
    async def process_request(self, topic: str, payload: dict[str, Any]) -> None:
        device_id = payload.get("device_id")
        async with AsyncSessionLocal() as session:
            bed = await self._get_bed_by_device_id(session, device_id)
            if not bed:
                return

            event = Event(
                bed_id=bed.id,
                event_type=EventType.REQUEST,
                priority=3,
                status=EventStatus.PENDING,
                payload=payload,
                source_topic=topic,
            )
            session.add(event)
            await session.commit()
            await session.refresh(event)

        await self._notification.broadcast({
            "type": "request",
            "event_id": str(event.id),
            "bed_id": bed.bed_number,
            "timestamp": payload.get("timestamp"),
        })

    # =========================================================
    # ÁUDIO
    # =========================================================
    async def process_audio(self, topic: str, payload: dict[str, Any]) -> None:
        """Recebe payload de áudio e encaminha para o serviço de transcrição."""
        device_id = payload.get("device_id")
        logger.info("Áudio recebido de %s (%ds)", device_id,
                    payload.get("duration_seconds", 0))

        # TODO (Fase 4): Encaminhar para audio_service.py
        # await audio_service.enqueue(payload)

    # =========================================================
    # AMBIENTE
    # =========================================================
    async def process_environment(self, topic: str, payload: dict[str, Any]) -> None:
        """Persiste leitura ambiental e notifica dashboard."""
        device_id = payload.get("device_id")
        logger.debug("Ambiente recebido de %s: T=%.1f H=%.1f",
                     device_id,
                     payload.get("temperature_celsius", 0),
                     payload.get("humidity_percent", 0))

        # TODO (Fase 2): Persistir em environment_readings
        # Notifica dashboard com dados atualizados
        async with AsyncSessionLocal() as session:
            bed = await self._get_bed_by_device_id(session, device_id)
            if bed:
                await self._notification.broadcast({
                    "type": "environment",
                    "bed_id": bed.bed_number,
                    "temperature_celsius": payload.get("temperature_celsius"),
                    "humidity_percent": payload.get("humidity_percent"),
                    "luminosity_lux": payload.get("luminosity_lux"),
                    "noise_level_db": payload.get("noise_level_db"),
                    "motion_detected": payload.get("motion_detected"),
                })

    # =========================================================
    # IMAGEM
    # =========================================================
    async def process_image(self, topic: str, payload: dict[str, Any]) -> None:
        """Recebe frame JPEG base64 e encaminha para media_service."""
        logger.info("Imagem recebida de %s", payload.get("device_id"))
        # TODO (Fase 5): await media_service.store_frame(payload)

    # =========================================================
    # LOG
    # =========================================================
    async def process_log(self, topic: str, payload: dict[str, Any]) -> None:
        """Registra log de dispositivo (QoS 0 — best effort)."""
        logger.debug("[DEVICE LOG] %s: %s",
                     payload.get("device_id"), payload.get("message"))

    # =========================================================
    # HELPERS
    # =========================================================
    async def _get_bed_by_device_id(self, session, device_id: str) -> Bed | None:
        from sqlalchemy import select
        result = await session.execute(
            select(Bed).where(Bed.device_id == device_id)
        )
        return result.scalar_one_or_none()
