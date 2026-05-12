"""
VitalKare — Rotas de Pacientes (/api/v1/patients)
"""

import uuid
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.db.session import get_db
from app.dependencies import get_current_operator, require_role
from app.models.patient import Patient, BedAssignment
from app.models.operator import OperatorRole

router = APIRouter(prefix="/patients", tags=["Pacientes"])


@router.get("/")
async def list_patients(
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(get_current_operator),
):
    result = await db.execute(select(Patient))
    return [_patient_to_dict(p) for p in result.scalars().all()]


@router.get("/{patient_id}")
async def get_patient(
    patient_id: uuid.UUID,
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(get_current_operator),
):
    return _patient_to_dict(await _get_or_404(db, patient_id))


@router.post("/", status_code=status.HTTP_201_CREATED)
async def create_patient(
    data: dict,
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(require_role(OperatorRole.ADMIN, OperatorRole.NURSE, OperatorRole.DOCTOR)),
):
    patient = Patient(
        name=data["name"],
        birth_date=data.get("birth_date"),
        medical_record_number=data.get("medical_record_number"),
    )
    db.add(patient)
    await db.commit()
    await db.refresh(patient)
    return _patient_to_dict(patient)


@router.post("/{patient_id}/assign")
async def assign_to_bed(
    patient_id: uuid.UUID,
    data: dict,
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(require_role(OperatorRole.ADMIN, OperatorRole.NURSE)),
):
    """Aloca paciente a um leito."""
    patient = await _get_or_404(db, patient_id)
    bed_id = data.get("bed_id")
    if not bed_id:
        raise HTTPException(status_code=400, detail="bed_id é obrigatório")

    # Desativa alocação ativa anterior do leito
    result = await db.execute(
        select(BedAssignment).where(
            BedAssignment.bed_id == bed_id,
            BedAssignment.is_active.is_(True),
        )
    )
    for old in result.scalars().all():
        old.is_active = False

    assignment = BedAssignment(bed_id=bed_id, patient_id=patient.id)
    db.add(assignment)
    await db.commit()
    return {"patient_id": str(patient_id), "bed_id": str(bed_id), "assigned": True}


# =========================================================
# HELPERS
# =========================================================
async def _get_or_404(db: AsyncSession, patient_id: uuid.UUID) -> Patient:
    result = await db.execute(select(Patient).where(Patient.id == patient_id))
    p = result.scalar_one_or_none()
    if not p:
        raise HTTPException(status_code=404, detail="Paciente não encontrado")
    return p


def _patient_to_dict(p: Patient) -> dict:
    return {
        "id": str(p.id),
        "name": p.name,
        "birth_date": p.birth_date.isoformat() if p.birth_date else None,
        "medical_record_number": p.medical_record_number,
        "admission_date": p.admission_date.isoformat() if p.admission_date else None,
        "discharge_date": p.discharge_date.isoformat() if p.discharge_date else None,
    }
