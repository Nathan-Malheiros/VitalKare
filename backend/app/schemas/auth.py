"""
VitalKare — Schemas Pydantic v2: Autenticação
"""
from pydantic import BaseModel, EmailStr


class LoginRequest(BaseModel):
    username: EmailStr
    password: str


class TokenResponse(BaseModel):
    access_token: str
    refresh_token: str
    token_type: str = "bearer"


class TokenPayload(BaseModel):
    sub: str
    role: str | None = None
    name: str | None = None
