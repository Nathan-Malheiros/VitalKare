"""
VitalKare Backend — Ponto de Entrada FastAPI
Versão: 1.0.0

Gerencia o ciclo de vida da aplicação (lifespan):
  - Conexão com banco de dados
  - Inicialização do cliente MQTT
  - Inicialização do heartbeat monitor
  - Registro de rotas
"""

from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.config import get_settings
from app.db.session import init_db
from app.core.mqtt_handler import MQTTHandler
from app.core.heartbeat_monitor import HeartbeatMonitor
from app.api.v1 import router as api_v1_router
from app.api.v1.websocket import router as ws_router

settings = get_settings()

# =========================================================
# LIFESPAN — startup / shutdown
# =========================================================
@asynccontextmanager
async def lifespan(app: FastAPI):
    """Inicializa serviços ao subir e limpa ao descer."""

    # Banco de dados
    await init_db()

    # MQTT Broker
    mqtt_handler = MQTTHandler()
    await mqtt_handler.connect()
    app.state.mqtt = mqtt_handler

    # Heartbeat monitor
    heartbeat_monitor = HeartbeatMonitor(mqtt_handler)
    await heartbeat_monitor.start()
    app.state.heartbeat = heartbeat_monitor

    yield  # Aplicação rodando

    # Shutdown
    await heartbeat_monitor.stop()
    await mqtt_handler.disconnect()


# =========================================================
# INSTÂNCIA FASTAPI
# =========================================================
app = FastAPI(
    title="VitalKare API",
    description="Plataforma Hospitalar Inteligente — Backend de Serviços",
    version="1.0.0",
    docs_url="/api/docs",
    redoc_url="/api/redoc",
    openapi_url="/api/openapi.json",
    lifespan=lifespan,
)

# =========================================================
# CORS
# =========================================================
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# =========================================================
# ROTAS
# =========================================================
app.include_router(api_v1_router, prefix="/api/v1")
app.include_router(ws_router, prefix="/ws")


@app.get("/health", tags=["Sistema"])
async def health_check():
    """Endpoint de health check para load balancer / Docker."""
    return {"status": "ok", "service": "vitalkare-backend", "version": "1.0.0"}
