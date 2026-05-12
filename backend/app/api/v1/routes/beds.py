"""
VitalKare — Rotas de Leitos (/api/v1/beds)
"""

import uuid
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.db.session import get_db
from app.dependencies import get_current_operator, require_role
from app.models.bed import Bed, BedStatus, Ward
from app.models.operator import OperatorRole

router = APIRouter(prefix="/beds", tags=["Leitos"])


@router.get("/")
async def list_beds(
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(get_current_operator),
    ward_id: uuid.UUID | None = None,
):
    """Lista todos os leitos, com filtro opcional por ala."""
    query = select(Bed)
    if ward_id:
        query = query.where(Bed.ward_id == ward_id)
    result = await db.execute(query)
    beds = result.scalars().all()
    return [_bed_to_dict(b) for b in beds]


@router.get("/{bed_id}")
async def get_bed(
    bed_id: uuid.UUID,
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(get_current_operator),
):
    bed = await _get_or_404(db, bed_id)
    return _bed_to_dict(bed)


@router.post("/", status_code=status.HTTP_201_CREATED)
async def create_bed(
    data: dict,
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(require_role(OperatorRole.ADMIN)),
):
    """Registra novo leito (apenas ADMIN)."""
    bed = Bed(
        ward_id=data["ward_id"],
        bed_number=data["bed_number"],
        device_id=data["device_id"],
    )
    db.add(bed)
    await db.commit()
    await db.refresh(bed)
    return _bed_to_dict(bed)


@router.patch("/{bed_id}/status")
async def update_bed_status(
    bed_id: uuid.UUID,
    data: dict,
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(require_role(OperatorRole.ADMIN, OperatorRole.NURSE)),
):
    """Atualiza manualmente o status de um leito."""
    bed = await _get_or_404(db, bed_id)
    new_status = data.get("status")
    if new_status not in BedStatus.__members__:
        raise HTTPException(status_code=400, detail="Status inválido")
    bed.status = BedStatus(new_status)
    await db.commit()
    return {"bed_id": str(bed_id), "status": bed.status}


# =========================================================
# HELPERS
# =========================================================
async def _get_or_404(db: AsyncSession, bed_id: uuid.UUID) -> Bed:
    result = await db.execute(select(Bed).where(Bed.id == bed_id))
    bed = result.scalar_one_or_none()
    if not bed:
        raise HTTPException(status_code=404, detail="Leito não encontrado")
    return bed


def _bed_to_dict(bed: Bed) -> dict:
    return {
        "id": str(bed.id),
        "ward_id": str(bed.ward_id),
        "bed_number": bed.bed_number,
        "device_id": bed.device_id,
        "status": bed.status.value,
        "last_heartbeat": bed.last_heartbeat.isoformat() if bed.last_heartbeat else None,
        "firmware_version": bed.firmware_version,
        "wifi_rssi": bed.wifi_rssi,
        "free_heap_bytes": bed.free_heap_bytes,
    }
