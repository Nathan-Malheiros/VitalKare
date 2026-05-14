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
    # ÁUDIO (payload binário PCM 16-bit 16 kHz mono)
    # =========================================================
    async def process_audio(self, topic: str, raw_bytes: bytes) -> None:
        """Persiste gravação de áudio e notifica dashboard."""
        from app.models.audio_record import AudioRecord

        parts = topic.split("/")
        device_id = parts[2] if len(parts) >= 3 else None
        if not device_id:
            return

        size_bytes = len(raw_bytes)
        # 16 kHz * 2 bytes/amostra = 32 000 bytes/s
        duration_s = max(1, size_bytes // 32_000)

        logger.info("Áudio recebido de %s (%d bytes, ~%ds)",
                    device_id, size_bytes, duration_s)

        async with AsyncSessionLocal() as session:
            bed = await self._get_bed_by_device_id(session, device_id)
            if not bed:
                logger.warning("Áudio de dispositivo desconhecido: %s", device_id)
                return

            record = AudioRecord(
                bed_id=bed.id,
                duration_seconds=float(duration_s),
                file_size_bytes=size_bytes,
                sample_rate=16_000,
                # TODO (Fase 4): file_path após upload para MinIO
                # TODO (Fase 4): chamar Whisper e preencher Transcription
            )
            session.add(record)
            await session.commit()
            await session.refresh(record)

        await self._notification.broadcast({
            "type":             "audio_record",
            "bed_id":           bed.bed_number,
            "record_id":        str(record.id),
            "duration_seconds": duration_s,
            "transcript":       None,   # será preenchido pelo Whisper (Fase 4)
            "confidence":       None,
            "timestamp":        record.created_at.isoformat(),
        })

    # =========================================================
    # AMBIENTE
    # =========================================================
    async def process_environment(self, topic: str, payload: dict[str, Any]) -> None:
        """Persiste leitura ambiental, atualiza snapshot do leito e notifica dashboard."""
        from app.models.environment_reading import EnvironmentReading

        device_id = payload.get("device_id")
        logger.debug("Ambiente recebido de %s: T=%.1f H=%.1f Lux=%.0f Postura=%s",
                     device_id,
                     payload.get("temperature_celsius", 0),
                     payload.get("humidity_percent", 0),
                     payload.get("luminosity_lux", 0),
                     payload.get("posture_label", "-"))

        async with AsyncSessionLocal() as session:
            bed = await self._get_bed_by_device_id(session, device_id)
            if not bed:
                return

            # Persiste a leitura ambiental
            reading = EnvironmentReading(
                bed_id=bed.id,
                temperature_celsius=payload.get("temperature_celsius"),
                humidity_percent=payload.get("humidity_percent"),
                luminosity_lux=payload.get("luminosity_lux"),
                noise_level_db=payload.get("noise_level_db"),
                motion_detected=payload.get("motion_detected"),
                posture_label=payload.get("posture_label"),
            )
            session.add(reading)

            # Atualiza snapshot desnormalizado no leito para leitura rápida
            bed.metadata_json = {
                **(bed.metadata_json or {}),
                "last_temp":    payload.get("temperature_celsius"),
                "last_hum":     payload.get("humidity_percent"),
                "last_lux":     payload.get("luminosity_lux"),
                "last_noise":   payload.get("noise_level_db"),
                "last_motion":  payload.get("motion_detected"),
                "last_posture": payload.get("posture_label"),
            }

            await session.commit()

        # Notifica dashboard com todos os campos
        await self._notification.broadcast({
            "type":                "environment",
            "bed_id":             bed.bed_number,
            "temperature_celsius": payload.get("temperature_celsius"),
            "humidity_percent":    payload.get("humidity_percent"),
            "luminosity_lux":      payload.get("luminosity_lux"),
            "noise_level_db":      payload.get("noise_level_db"),
            "motion_detected":     payload.get("motion_detected"),
            "posture_label":       payload.get("posture_label"),
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
