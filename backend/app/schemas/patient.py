"""
VitalKare — Schemas Pydantic v2: Patient e BedAssignment
"""
import uuid
from datetime import date, datetime

from pydantic import BaseModel, ConfigDict


class PatientCreate(BaseModel):
    name: str
    birth_date: date | None = None
    medical_record_number: str | None = None
    admission_date: datetime | None = None
    notes: str | None = None


class PatientRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    name: str
    birth_date: date | None
    medical_record_number: str | None
    admission_date: datetime | None
    discharge_date: datetime | None
    notes: str | None
    created_at: datetime


class PatientUpdate(BaseModel):
    name: str | None = None
    notes: str | None = None
    discharge_date: datetime | None = None


class BedAssignmentCreate(BaseModel):
    bed_id: uuid.UUID
    patient_id: uuid.UUID


class BedAssignmentRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    bed_id: uuid.UUID
    patient_id: uuid.UUID
    assigned_at: datetime
    released_at: datetime | None
    is_active: bool
