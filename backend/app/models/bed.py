"""
VitalKare — Modelos SQLAlchemy: Bed (Leito) e Ward (Ala)
"""

import uuid
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy import String, Integer, Boolean, DateTime, ForeignKey, JSON, Enum as SAEnum
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship
import enum

from app.db.session import Base


class BedStatus(str, enum.Enum):
    ONLINE    = "ONLINE"
    OFFLINE   = "OFFLINE"
    EMERGENCY = "EMERGENCY"
    OCCUPIED  = "OCCUPIED"
    FREE      = "FREE"


class Ward(Base):
    """Ala hospitalar — agrupa leitos."""
    __tablename__ = "wards"

    id:         Mapped[uuid.UUID]    = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    name:       Mapped[str]          = mapped_column(String(100), nullable=False)
    floor:      Mapped[Optional[int]]= mapped_column(Integer, nullable=True)
    capacity:   Mapped[int]          = mapped_column(Integer, default=0)
    created_at: Mapped[datetime]     = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(timezone.utc))

    beds: Mapped[list["Bed"]] = relationship("Bed", back_populates="ward")


class Bed(Base):
    """Leito hospitalar — nó inteligente IoT."""
    __tablename__ = "beds"

    id:               Mapped[uuid.UUID]     = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    ward_id:          Mapped[uuid.UUID]     = mapped_column(UUID(as_uuid=True), ForeignKey("wards.id"), nullable=False)
    bed_number:       Mapped[str]           = mapped_column(String(20), unique=True, nullable=False)
    device_id:        Mapped[str]           = mapped_column(String(50), unique=True, nullable=False)
    status:           Mapped[BedStatus]     = mapped_column(SAEnum(BedStatus), default=BedStatus.OFFLINE)
    last_heartbeat:   Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)
    firmware_version: Mapped[Optional[str]] = mapped_column(String(20), nullable=True)
    wifi_rssi:        Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    free_heap_bytes:  Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    metadata_json:    Mapped[Optional[dict]]= mapped_column(JSON, nullable=True)
    created_at:       Mapped[datetime]      = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(timezone.utc))

    ward:                Mapped["Ward"]                      = relationship("Ward", back_populates="beds")
    events:              Mapped[list["Event"]]               = relationship("Event", back_populates="bed")  # type: ignore
    assignments:         Mapped[list["BedAssignment"]]       = relationship("BedAssignment", back_populates="bed")  # type: ignore
    audio_records:       Mapped[list["AudioRecord"]]         = relationship("AudioRecord", back_populates="bed")  # type: ignore
    environment_readings: Mapped[list["EnvironmentReading"]] = relationship("EnvironmentReading", back_populates="bed")  # type: ignore
