"""
VitalKare — Modelos SQLAlchemy: Patient (Paciente) e BedAssignment (Alocação)
"""

import uuid
import enum
from datetime import datetime, date
from typing import Optional

from sqlalchemy import String, Date, DateTime, ForeignKey, Boolean, Text
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.session import Base


class Patient(Base):
    __tablename__ = "patients"

    id:                    Mapped[uuid.UUID]      = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    name:                  Mapped[str]            = mapped_column(String(200), nullable=False)
    birth_date:            Mapped[Optional[date]] = mapped_column(Date, nullable=True)
    medical_record_number: Mapped[Optional[str]]  = mapped_column(String(50), unique=True, nullable=True)
    admission_date:        Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)
    discharge_date:        Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)
    notes:                 Mapped[Optional[str]]  = mapped_column(Text, nullable=True)
    created_at:            Mapped[datetime]       = mapped_column(DateTime(timezone=True), default=datetime.utcnow)

    assignments: Mapped[list["BedAssignment"]] = relationship("BedAssignment", back_populates="patient")


class BedAssignment(Base):
    """Alocação de um paciente a um leito (histórico)."""
    __tablename__ = "bed_assignments"

    id:          Mapped[uuid.UUID]          = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    bed_id:      Mapped[uuid.UUID]          = mapped_column(UUID(as_uuid=True), ForeignKey("beds.id"), nullable=False)
    patient_id:  Mapped[uuid.UUID]          = mapped_column(UUID(as_uuid=True), ForeignKey("patients.id"), nullable=False)
    assigned_at: Mapped[datetime]           = mapped_column(DateTime(timezone=True), default=datetime.utcnow)
    released_at: Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)
    is_active:   Mapped[bool]               = mapped_column(Boolean, default=True)

    bed:     Mapped["Bed"]     = relationship("Bed")        # type: ignore
    patient: Mapped["Patient"] = relationship("Patient", back_populates="assignments")
