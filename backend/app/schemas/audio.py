"""
VitalKare — Schemas Pydantic v2: AudioRecord e Transcription
"""
import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict


class AudioRecordRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    event_id: uuid.UUID | None
    bed_id: uuid.UUID
    file_path: str | None
    duration_seconds: float | None
    sample_rate: int | None
    file_size_bytes: int | None
    created_at: datetime


class TranscriptionRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    audio_record_id: uuid.UUID
    text: str | None
    confidence: float | None
    language: str | None
    model_used: str | None
    processing_time_ms: int | None
    created_at: datetime
