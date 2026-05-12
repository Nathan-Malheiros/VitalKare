"""
VitalKare — Dependências FastAPI injetadas via Depends()
"""

from typing import Annotated

from fastapi import Depends, HTTPException, status
from fastapi.security import OAuth2PasswordBearer
from jose import JWTError
from sqlalchemy.ext.asyncio import AsyncSession

from app.db.session import get_db
from app.models.operator import Operator, OperatorRole
from app.utils.security import decode_token

oauth2_scheme = OAuth2PasswordBearer(tokenUrl="/api/v1/auth/token")


async def get_current_operator(
    token: Annotated[str, Depends(oauth2_scheme)],
    db: Annotated[AsyncSession, Depends(get_db)],
) -> Operator:
    """Valida JWT e retorna o operador autenticado."""
    credentials_exception = HTTPException(
        status_code=status.HTTP_401_UNAUTHORIZED,
        detail="Token de autenticação inválido ou expirado",
        headers={"WWW-Authenticate": "Bearer"},
    )
    try:
        payload = decode_token(token)
        operator_id: str | None = payload.get("sub")
        if not operator_id:
            raise credentials_exception
    except JWTError:
        raise credentials_exception

    from sqlalchemy import select
    result = await db.execute(
        select(Operator).where(Operator.id == operator_id, Operator.is_active.is_(True))
    )
    operator = result.scalar_one_or_none()
    if operator is None:
        raise credentials_exception

    return operator


def require_role(*roles: OperatorRole):
    """Factory de dependência para verificação de papel (RBAC)."""
    async def _check(
        current: Annotated[Operator, Depends(get_current_operator)]
    ) -> Operator:
        if current.role not in roles:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Permissão insuficiente para esta ação",
            )
        return current
    return _check
