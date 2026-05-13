"""
VitalKare — Modelos SQLAlchemy: Event (Evento)
"""

import uuid
import enum
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy import String, Integer, DateTime, ForeignKey, JSON, Enum as SAEnum
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.session import Base


class EventType(str, enum.Enum):
    EMERGENCY    = "EMERGENCY"
    REQUEST      = "REQUEST"
    AUDIO        = "AUDIO"
    ENVIRONMENTAL= "ENVIRONMENTAL"
    SYSTEM       = "SYSTEM"


class EventStatus(str, enum.Enum):
    PENDING      = "PENDING"
    ACKNOWLEDGED = "ACKNOWLEDGED"
    RESOLVED     = "RESOLVED"
    EXPIRED      = "EXPIRED"


class Event(Base):
    __tablename__ = "events"

    id:          Mapped[uuid.UUID]      = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    bed_id:      Mapped[uuid.UUID]      = mapped_column(UUID(as_uuid=True), ForeignKey("beds.id"), nullable=False)
    event_type:  Mapped[EventType]      = mapped_column(SAEnum(EventType), nullable=False)
    priority:    Mapped[int]            = mapped_column(Integer, default=5)  # 1=crítico, 5=info
    status:      Mapped[EventStatus]    = mapped_column(SAEnum(EventStatus), default=EventStatus.PENDING)
    payload:     Mapped[Optional[dict]] = mapped_column(JSON, nullable=True)
    source_topic:Mapped[Optional[str]]  = mapped_column(String(200), nullable=True)
    created_at:  Mapped[datetime]       = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(timezone.utc))
    resolved_at: Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)

    bed:             Mapped["Bed"]                   = relationship("Bed", back_populates="events")  # type: ignore
    acknowledgements:Mapped[list["Acknowledgement"]] = relationship("Acknowledgement", back_populates="event")
    audio_records:   Mapped[list["AudioRecord"]]     = relationship("AudioRecord", back_populates="event")  # type: ignore


class Acknowledgement(Base):
    """Registro de confirmação de atendimento por um operador."""
    __tablename__ = "acknowledgements"

    id:                    Mapped[uuid.UUID]     = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    event_id:              Mapped[uuid.UUID]     = mapped_column(UUID(as_uuid=True), ForeignKey("events.id"), nullable=False)
    operator_id:           Mapped[uuid.UUID]     = mapped_column(UUID(as_uuid=True), ForeignKey("operators.id"), nullable=False)
    ack_at:                Mapped[datetime]      = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(timezone.utc))
    response_time_seconds: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    notes:                 Mapped[Optional[str]] = mapped_column(String(1000), nullable=True)

    event:    Mapped["Event"]    = relationship("Event", back_populates="acknowledgements")
    operator: Mapped["Operator"] = relationship("Operator")  # type: ignore
