"""
VitalKare — Models: AudioRecord e Transcription
"""
import uuid
from datetime import datetime, timezone

from sqlalchemy import BigInteger, Float, ForeignKey, Integer, String, Text
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.session import Base


class AudioRecord(Base):
    """Registros de áudio capturados pelo dispositivo KIIRA."""

    __tablename__ = "audio_records"

    id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), primary_key=True, default=uuid.uuid4
    )
    # FK para o evento que originou o áudio (opcional — pode ser captura passiva)
    event_id: Mapped[uuid.UUID | None] = mapped_column(
        UUID(as_uuid=True),
        ForeignKey("events.id", ondelete="SET NULL"),
        nullable=True,
        index=True,
    )
    bed_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True),
        ForeignKey("beds.id", ondelete="RESTRICT"),
        nullable=False,
        index=True,
    )
    # Caminho no MinIO: bucket/ward_id/bed_id/uuid.wav
    file_path: Mapped[str | None] = mapped_column(String(500), nullable=True)
    duration_seconds: Mapped[float | None] = mapped_column(Float, nullable=True)
    sample_rate: Mapped[int | None] = mapped_column(Integer, default=16000, nullable=True)
    file_size_bytes: Mapped[int | None] = mapped_column(BigInteger, nullable=True)
    checksum_sha256: Mapped[str | None] = mapped_column(String(64), nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        default=lambda: datetime.now(timezone.utc)
    )

    # Relacionamentos
    bed = relationship("Bed", back_populates="audio_records")
    event = relationship("Event", back_populates="audio_records")
    transcriptions: Mapped[list["Transcription"]] = relationship(
        "Transcription", back_populates="audio_record", cascade="all, delete-orphan"
    )


class Transcription(Base):
    """Resultado da transcrição de áudio pelo Whisper."""

    __tablename__ = "transcriptions"

    id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), primary_key=True, default=uuid.uuid4
    )
    audio_record_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True),
        ForeignKey("audio_records.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    text: Mapped[str | None] = mapped_column(Text, nullable=True)
    # Confiança do modelo (0.0 – 1.0)
    confidence: Mapped[float | None] = mapped_column(Float, nullable=True)
    language: Mapped[str | None] = mapped_column(String(10), default="pt", nullable=True)
    model_used: Mapped[str | None] = mapped_column(String(100), nullable=True)
    processing_time_ms: Mapped[int | None] = mapped_column(Integer, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        default=lambda: datetime.now(timezone.utc)
    )

    audio_record: Mapped[AudioRecord] = relationship(
        "AudioRecord", back_populates="transcriptions"
    )
