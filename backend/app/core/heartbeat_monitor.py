"""
VitalKare — Monitor de Heartbeat

Verifica periodicamente quais dispositivos não enviaram
heartbeat recentemente e muda seu status para OFFLINE.

Thresholds (configuráveis via .env):
  - OFFLINE   : > heartbeat_offline_threshold_s  (padrão 90s)
  - CRITICAL  : > heartbeat_critical_threshold_s (padrão 300s)
"""

import asyncio
import logging
from datetime import datetime, timezone, timedelta

from app.config import get_settings
from app.core.notification import NotificationService
from app.db.session import AsyncSessionLocal
from app.models.bed import Bed, BedStatus

logger = logging.getLogger("vitalkare.heartbeat")
settings = get_settings()


class HeartbeatMonitor:
    def __init__(self, mqtt_handler=None):
        self._mqtt = mqtt_handler
        self._notification = NotificationService()
        self._task: asyncio.Task | None = None

    async def start(self) -> None:
        self._task = asyncio.create_task(self._run())
        logger.info(
            "HeartbeatMonitor iniciado (offline=%ds, critical=%ds, check=%ds)",
            settings.heartbeat_offline_threshold_s,
            settings.heartbeat_critical_threshold_s,
            settings.heartbeat_check_interval_s,
        )

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    async def _run(self) -> None:
        while True:
            await asyncio.sleep(settings.heartbeat_check_interval_s)
            await self._check_all()

    async def _check_all(self) -> None:
        from sqlalchemy import select

        now = datetime.now(timezone.utc)
        offline_threshold = now - timedelta(
            seconds=settings.heartbeat_offline_threshold_s
        )
        critical_threshold = now - timedelta(
            seconds=settings.heartbeat_critical_threshold_s
        )

        async with AsyncSessionLocal() as session:
            result = await session.execute(
                select(Bed).where(
                    Bed.status != BedStatus.OFFLINE,
                    Bed.status != BedStatus.EMERGENCY,
                )
            )
            beds: list[Bed] = result.scalars().all()

            for bed in beds:
                if bed.last_heartbeat is None:
                    continue

                lh = bed.last_heartbeat
                if lh.tzinfo is None:
                    lh = lh.replace(tzinfo=timezone.utc)

                if lh < critical_threshold:
                    if bed.status != BedStatus.OFFLINE:
                        bed.status = BedStatus.OFFLINE
                        logger.warning(
                            "Dispositivo %s CRÍTICO — sem heartbeat há >%ds",
                            bed.device_id, settings.heartbeat_critical_threshold_s
                        )
                        await self._notification.broadcast({
                            "type": "bed_status",
                            "bed_id": bed.bed_number,
                            "status": "CRITICAL_OFFLINE",
                            "last_seen": lh.isoformat(),
                        })

                elif lh < offline_threshold:
                    if bed.status not in (BedStatus.OFFLINE,):
                        bed.status = BedStatus.OFFLINE
                        logger.info(
                            "Dispositivo %s OFFLINE — sem heartbeat há >%ds",
                            bed.device_id, settings.heartbeat_offline_threshold_s
                        )
                        await self._notification.broadcast({
                            "type": "bed_status",
                            "bed_id": bed.bed_number,
                            "status": "OFFLINE",
                            "last_seen": lh.isoformat(),
                        })

            await session.commit()
