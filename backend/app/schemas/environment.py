"""
VitalKare — Schemas Pydantic v2: EnvironmentReading
"""
import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict


class EnvironmentReadingRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    bed_id: uuid.UUID
    temperature_celsius: float | None
    humidity_percent: float | None
    luminosity_lux: float | None
    noise_level_db: float | None
    motion_detected: bool | None
    recorded_at: datetime


class EnvironmentReadingCreate(BaseModel):
    bed_id: uuid.UUID
    temperature_celsius: float | None = None
    humidity_percent: float | None = None
    luminosity_lux: float | None = None
    noise_level_db: float | None = None
    motion_detected: bool = False
