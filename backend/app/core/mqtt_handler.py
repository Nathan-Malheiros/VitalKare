"""
VitalKare — Handler MQTT do Backend

Assina os tópicos hospital/+/+/# e roteia mensagens para
o event_processor conforme o subtópico.
"""

import asyncio
import json
import logging
from typing import Optional

import aiomqtt

from app.config import get_settings
from app.core.event_processor import EventProcessor

logger = logging.getLogger("vitalkare.mqtt")
settings = get_settings()

# Tópicos assinados pelo backend
SUBSCRIPTIONS = [
    ("hospital/+/+/status",       1),
    ("hospital/+/+/emergency",    2),
    ("hospital/+/+/request",      1),
    ("hospital/+/+/audio",        1),
    ("hospital/+/+/environment",  1),
    ("hospital/+/+/image",        1),
    ("hospital/+/+/log",          0),
]


class MQTTHandler:
    """Gerencia a conexão MQTT do backend e processa mensagens recebidas."""

    def __init__(self):
        self._client: Optional[aiomqtt.Client] = None
        self._task: Optional[asyncio.Task] = None
        self._processor = EventProcessor()

    async def connect(self) -> None:
        """Inicia a conexão MQTT e a task de consumo de mensagens."""
        logger.info(
            "Conectando ao MQTT broker %s:%d",
            settings.mqtt_host, settings.mqtt_port
        )
        self._task = asyncio.create_task(self._run())

    async def disconnect(self) -> None:
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
        logger.info("MQTT handler desconectado")

    async def publish(self, topic: str, payload: str | bytes,
                      qos: int = 1, retain: bool = False) -> None:
        """Publica mensagem em um tópico (ex: comandos para dispositivos)."""
        if self._client:
            await self._client.publish(topic, payload, qos=qos, retain=retain)

    # =========================================================
    # LOOP INTERNO
    # =========================================================
    async def _run(self) -> None:
        reconnect_delay = 2

        while True:
            try:
                async with aiomqtt.Client(
                    hostname=settings.mqtt_host,
                    port=settings.mqtt_port,
                    identifier=settings.mqtt_client_id,
                    keepalive=settings.mqtt_keepalive,
                    username=settings.mqtt_username or None,
                    password=settings.mqtt_password or None,
                ) as client:
                    self._client = client
                    reconnect_delay = 2  # reset após conexão bem-sucedida
                    logger.info("MQTT broker conectado")

                    # Assinar tópicos
                    for topic, qos in SUBSCRIPTIONS:
                        await client.subscribe(topic, qos=qos)
                        logger.debug("Assinado: %s (QoS %d)", topic, qos)

                    # Consumir mensagens
                    async for message in client.messages:
                        await self._dispatch(
                            str(message.topic),
                            message.payload,
                        )

            except aiomqtt.MqttError as exc:
                logger.warning(
                    "Conexão MQTT perdida (%s). Reconectando em %ds...",
                    exc, reconnect_delay
                )
                self._client = None
                await asyncio.sleep(reconnect_delay)
                reconnect_delay = min(reconnect_delay * 2, 120)

            except asyncio.CancelledError:
                break

    async def _dispatch(self, topic: str, raw_payload: bytes) -> None:
        """Roteia mensagem para o processor correto com base no subtópico."""
        parts = topic.split("/")
        # Formato: hospital/{ward}/{device_id}/{subtopic}
        if len(parts) < 4:
            return

        subtopic = parts[3]

        # Áudio: payload binário PCM — NÃO é JSON, trata antes do decode
        if subtopic == "audio":
            await self._processor.process_audio(topic=topic, raw_bytes=raw_payload)
            return

        try:
            payload = json.loads(raw_payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            logger.warning("Payload inválido em %s (tamanho=%d bytes)", topic, len(raw_payload))
            return

        handlers = {
            "status":      self._processor.process_status,
            "emergency":   self._processor.process_emergency,
            "request":     self._processor.process_request,
            "environment": self._processor.process_environment,
            "image":       self._processor.process_image,
            "log":         self._processor.process_log,
        }

        handler = handlers.get(subtopic)
        if handler:
            await handler(topic=topic, payload=payload)
        else:
            logger.debug("Subtópico sem handler: %s", subtopic)
