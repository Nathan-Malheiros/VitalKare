"""
VitalKare — Rotas de Eventos (/api/v1/events)
"""

import uuid
from datetime import datetime, timezone
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select, desc
from sqlalchemy.ext.asyncio import AsyncSession

from app.db.session import get_db
from app.dependencies import get_current_operator
from app.models.event import Event, EventStatus
from app.models.operator import Operator

router = APIRouter(prefix="/events", tags=["Eventos"])


@router.get("/")
async def list_events(
    db: Annotated[AsyncSession, Depends(get_db)],
    _: Annotated = Depends(get_current_operator),
    status: str | None = None,
    limit: int = 50,
    offset: int = 0,
):
    """Lista eventos com filtro por status e paginação."""
    query = select(Event).order_by(desc(Event.created_at)).offset(offset).limit(limit)
    if status and status in EventStatus.__members__:
        query = query.where(Event.status == EventStatus(status))
    result = await db.execute(query)
    events = result.scalars().all()
    return [_event_to_dict(e) for e in events]


@router.post("/{event_id}/acknowledge")
async def acknowledge_event(
    event_id: uuid.UUID,
    data: dict,
    db: Annotated[AsyncSession, Depends(get_db)],
    operator: Annotated[Operator, Depends(get_current_operator)],
):
    """Registra confirmação de atendimento de um evento."""
    from app.models.event import Acknowledgement

    result = await db.execute(select(Event).where(Event.id == event_id))
    event: Event | None = result.scalar_one_or_none()
    if not event:
        raise HTTPException(status_code=404, detail="Evento não encontrado")

    if event.status == EventStatus.RESOLVED:
        raise HTTPException(status_code=400, detail="Evento já foi resolvido")

    response_time = None
    if event.created_at:
        created = event.created_at
        if created.tzinfo is None:
            created = created.replace(tzinfo=timezone.utc)
        response_time = int((datetime.now(timezone.utc) - created).total_seconds())

    ack = Acknowledgement(
        event_id=event.id,
        operator_id=operator.id,
        response_time_seconds=response_time,
        notes=data.get("notes"),
    )
    event.status = EventStatus.ACKNOWLEDGED
    db.add(ack)
    await db.commit()

    return {"event_id": str(event_id), "status": "ACKNOWLEDGED", "response_time_seconds": response_time}


@router.post("/{event_id}/resolve")
async def resolve_event(
    event_id: uuid.UUID,
    db: Annotated[AsyncSession, Depends(get_db)],
    operator: Annotated[Operator, Depends(get_current_operator)],
):
    """Marca evento como resolvido."""
    result = await db.execute(select(Event).where(Event.id == event_id))
    event: Event | None = result.scalar_one_or_none()
    if not event:
        raise HTTPException(status_code=404, detail="Evento não encontrado")

    event.status = EventStatus.RESOLVED
    event.resolved_at = datetime.now(timezone.utc)
    await db.commit()

    return {"event_id": str(event_id), "status": "RESOLVED"}


# =========================================================
# HELPERS
# =========================================================
def _event_to_dict(event: Event) -> dict:
    return {
        "id": str(event.id),
        "bed_id": str(event.bed_id),
        "event_type": event.event_type.value,
        "priority": event.priority,
        "status": event.status.value,
        "payload": event.payload,
        "source_topic": event.source_topic,
        "created_at": event.created_at.isoformat() if event.created_at else None,
        "resolved_at": event.resolved_at.isoformat() if event.resolved_at else None,
    }
