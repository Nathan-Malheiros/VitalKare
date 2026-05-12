"""
VitalKare — Modelo SQLAlchemy: Operator (Equipe)
"""

import uuid
import enum
from datetime import datetime
from typing import Optional

from sqlalchemy import String, Boolean, DateTime, Enum as SAEnum
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column

from app.db.session import Base


class OperatorRole(str, enum.Enum):
    ADMIN   = "ADMIN"
    NURSE   = "NURSE"
    DOCTOR  = "DOCTOR"
    VIEWER  = "VIEWER"


class Operator(Base):
    __tablename__ = "operators"

    id:            Mapped[uuid.UUID]          = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    name:          Mapped[str]                = mapped_column(String(200), nullable=False)
    email:         Mapped[str]                = mapped_column(String(200), unique=True, nullable=False)
    role:          Mapped[OperatorRole]       = mapped_column(SAEnum(OperatorRole), default=OperatorRole.VIEWER)
    password_hash: Mapped[str]                = mapped_column(String(200), nullable=False)
    is_active:     Mapped[bool]               = mapped_column(Boolean, default=True)
    last_login:    Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)
    created_at:    Mapped[datetime]           = mapped_column(DateTime(timezone=True), default=datetime.utcnow)
