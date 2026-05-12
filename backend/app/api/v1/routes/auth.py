"""
VitalKare — Rota de autenticação (/api/v1/auth)
"""

from typing import Annotated
from datetime import datetime, timezone

from fastapi import APIRouter, Depends, HTTPException, status
from fastapi.security import OAuth2PasswordRequestForm
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.db.session import get_db
from app.models.operator import Operator
from app.utils.security import verify_password, create_access_token, create_refresh_token

router = APIRouter(prefix="/auth", tags=["Autenticação"])


@router.post("/token")
async def login(
    form: Annotated[OAuth2PasswordRequestForm, Depends()],
    db: Annotated[AsyncSession, Depends(get_db)],
):
    """Login com e-mail + senha. Retorna access e refresh tokens."""
    result = await db.execute(
        select(Operator).where(Operator.email == form.username, Operator.is_active.is_(True))
    )
    operator: Operator | None = result.scalar_one_or_none()

    if not operator or not verify_password(form.password, operator.password_hash):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Credenciais inválidas",
            headers={"WWW-Authenticate": "Bearer"},
        )

    operator.last_login = datetime.now(timezone.utc)
    await db.commit()

    access_token = create_access_token(
        subject=str(operator.id),
        extra_claims={"role": operator.role.value, "name": operator.name},
    )
    refresh_token = create_refresh_token(subject=str(operator.id))

    return {
        "access_token": access_token,
        "refresh_token": refresh_token,
        "token_type": "bearer",
    }
