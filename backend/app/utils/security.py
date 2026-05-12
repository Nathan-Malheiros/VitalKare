"""
VitalKare — Utilitários de segurança: hashing de senhas e JWT
"""

from datetime import datetime, timezone, timedelta
from typing import Any

import bcrypt
from jose import JWTError, jwt

from app.config import get_settings

settings = get_settings()


# =========================================================
# PASSWORD
# =========================================================

def hash_password(plain: str) -> str:
    """Retorna hash bcrypt da senha em texto puro."""
    salt = bcrypt.gensalt(rounds=12)
    return bcrypt.hashpw(plain.encode(), salt).decode()


def verify_password(plain: str, hashed: str) -> bool:
    """Compara senha em texto puro com hash bcrypt."""
    return bcrypt.checkpw(plain.encode(), hashed.encode())


# =========================================================
# JWT
# =========================================================

def create_access_token(subject: str, extra_claims: dict[str, Any] | None = None) -> str:
    """
    Gera JWT de acesso.

    Args:
        subject: Identificador do usuário (UUID ou e-mail).
        extra_claims: Claims adicionais (role, nome, etc.).
    """
    now = datetime.now(timezone.utc)
    expire = now + timedelta(minutes=settings.access_token_expire_minutes)

    payload: dict[str, Any] = {
        "sub": subject,
        "iat": now,
        "exp": expire,
        "type": "access",
    }
    if extra_claims:
        payload.update(extra_claims)

    return jwt.encode(payload, settings.secret_key, algorithm=settings.algorithm)


def create_refresh_token(subject: str) -> str:
    """Gera JWT de refresh com expiração estendida."""
    now = datetime.now(timezone.utc)
    expire = now + timedelta(hours=settings.refresh_token_expire_hours)
    payload = {
        "sub": subject,
        "iat": now,
        "exp": expire,
        "type": "refresh",
    }
    return jwt.encode(payload, settings.secret_key, algorithm=settings.algorithm)


def decode_token(token: str) -> dict[str, Any]:
    """
    Decodifica e valida JWT.

    Raises:
        JWTError: Se o token for inválido, expirado ou mal-formado.
    """
    return jwt.decode(token, settings.secret_key, algorithms=[settings.algorithm])
