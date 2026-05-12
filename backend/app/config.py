"""
VitalKare — Configurações da aplicação via pydantic-settings

Lê variáveis de ambiente (ou arquivo .env).
Nunca hardcode segredos aqui — use .env ou secrets do Docker.
"""

from functools import lru_cache
from typing import List

from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        case_sensitive=False,
    )

    # =========================================================
    # APLICAÇÃO
    # =========================================================
    app_name: str = "VitalKare Backend"
    app_version: str = "1.0.0"
    debug: bool = False
    environment: str = "development"  # development | staging | production

    # =========================================================
    # BANCO DE DADOS
    # =========================================================
    database_url: str = "postgresql+asyncpg://vitalkare:vitalkare@localhost:5432/vitalkare"
    db_pool_size: int = 10
    db_max_overflow: int = 20
    db_echo_sql: bool = False

    # =========================================================
    # REDIS
    # =========================================================
    redis_url: str = "redis://localhost:6379/0"

    # =========================================================
    # MQTT
    # =========================================================
    mqtt_host: str = "localhost"
    mqtt_port: int = 1883               # 8883 em produção (TLS)
    mqtt_use_tls: bool = False
    mqtt_username: str = ""
    mqtt_password: str = ""
    mqtt_client_id: str = "vitalkare-backend"
    mqtt_keepalive: int = 60

    # =========================================================
    # SEGURANÇA — JWT
    # =========================================================
    secret_key: str = "CHANGE_ME_IN_PRODUCTION_USE_256BIT_RANDOM_KEY"
    algorithm: str = "HS256"
    access_token_expire_minutes: int = 60
    refresh_token_expire_hours: int = 24

    # =========================================================
    # STORAGE — MinIO / S3
    # =========================================================
    storage_endpoint: str = "localhost:9000"
    storage_access_key: str = "minioadmin"
    storage_secret_key: str = "minioadmin"
    storage_bucket_audio: str = "vitalkare-audio"
    storage_bucket_images: str = "vitalkare-images"
    storage_use_ssl: bool = False

    # =========================================================
    # TRANSCRIÇÃO
    # =========================================================
    whisper_model: str = "small"        # tiny, base, small, medium, large
    whisper_language: str = "pt"
    whisper_device: str = "cpu"         # cpu | cuda

    # =========================================================
    # HEARTBEAT MONITOR
    # =========================================================
    heartbeat_offline_threshold_s: int = 90
    heartbeat_critical_threshold_s: int = 300
    heartbeat_check_interval_s: int = 15

    # =========================================================
    # CORS
    # =========================================================
    cors_origins: List[str] = [
        "http://localhost:5173",    # Vite dev server
        "http://localhost:3000",
    ]

    @field_validator("cors_origins", mode="before")
    @classmethod
    def parse_cors_origins(cls, v):
        if isinstance(v, str):
            return [origin.strip() for origin in v.split(",")]
        return v

    @field_validator("secret_key")
    @classmethod
    def warn_default_secret(cls, v):
        if v == "CHANGE_ME_IN_PRODUCTION_USE_256BIT_RANDOM_KEY":
            import warnings
            warnings.warn(
                "SECRET_KEY está com valor padrão inseguro! "
                "Configure em produção.",
                UserWarning,
                stacklevel=2,
            )
        return v


@lru_cache
def get_settings() -> Settings:
    """Retorna instância cacheada das configurações."""
    return Settings()
