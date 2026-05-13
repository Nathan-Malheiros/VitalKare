"""
VitalKare — Schemas Pydantic v2: Event e Acknowledgement
"""
import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict

from app.models.event import EventStatus, EventType


class EventRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    bed_id: uuid.UUID
    event_type: EventType
    priority: int
    status: EventStatus
    payload: dict | None
    source_topic: str | None
    created_at: datetime
    resolved_at: datetime | None


class AckCreate(BaseModel):
    notes: str | None = None


class AckRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    event_id: uuid.UUID
    operator_id: uuid.UUID
    ack_at: datetime
    response_time_seconds: int | None
    notes: str | None
