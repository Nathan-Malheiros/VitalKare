"""
VitalKare — Schemas Pydantic v2: Ward e Bed
"""
import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict

from app.models.bed import BedStatus


# =========================================================
# WARD
# =========================================================

class WardRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    name: str
    floor: int | None
    capacity: int
    created_at: datetime


class WardCreate(BaseModel):
    name: str
    floor: int = 1
    capacity: int = 10


# =========================================================
# BED
# =========================================================

class BedRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    ward_id: uuid.UUID
    bed_number: str
    device_id: str
    status: BedStatus
    last_heartbeat: datetime | None
    firmware_version: str | None
    wifi_rssi: int | None
    free_heap_bytes: int | None
    created_at: datetime


class BedCreate(BaseModel):
    ward_id: uuid.UUID
    bed_number: str
    device_id: str


class BedStatusUpdate(BaseModel):
    status: BedStatus
