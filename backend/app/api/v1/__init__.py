"""
VitalKare — Router principal da API v1
"""

from fastapi import APIRouter

from app.api.v1.routes.auth import router as auth_router
from app.api.v1.routes.beds import router as beds_router
from app.api.v1.routes.events import router as events_router
from app.api.v1.routes.patients import router as patients_router

router = APIRouter()

router.include_router(auth_router)
router.include_router(beds_router)
router.include_router(events_router)
router.include_router(patients_router)
