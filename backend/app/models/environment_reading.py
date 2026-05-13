"""
VitalKare — Model: EnvironmentReading (série temporal — hipertabela TimescaleDB)
"""
import uuid
from datetime import datetime, timezone

from sqlalchemy import Boolean, Float, ForeignKey
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.session import Base


class EnvironmentReading(Base):
    """
    Leitura ambiental periódica enviada pelo sensor HDC1080 / LDR / MPU6050.

    Em produção, esta tabela é gerenciada como hipertabela TimescaleDB
    particionada por 'recorded_at' (chunk de 1 dia).
    A migração Alembic executa: SELECT create_hypertable('environment_readings', 'recorded_at')
    """

    __tablename__ = "environment_readings"

    id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), primary_key=True, default=uuid.uuid4
    )
    bed_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True),
        ForeignKey("beds.id", ondelete="RESTRICT"),
        nullable=False,
        index=True,
    )
    # Temperatura (HDC1080) — graus Celsius
    temperature_celsius: Mapped[float | None] = mapped_column(Float, nullable=True)
    # Umidade relativa (HDC1080) — %
    humidity_percent: Mapped[float | None] = mapped_column(Float, nullable=True)
    # Luminosidade (LDR) — lux
    luminosity_lux: Mapped[float | None] = mapped_column(Float, nullable=True)
    # Nível de ruído (microfone analógico) — dB
    noise_level_db: Mapped[float | None] = mapped_column(Float, nullable=True)
    # Detecção de movimento (MPU6050)
    motion_detected: Mapped[bool | None] = mapped_column(
        Boolean, default=False, nullable=True
    )
    # Coluna de tempo usada para particionamento TimescaleDB
    recorded_at: Mapped[datetime] = mapped_column(
        primary_key=True,
        default=lambda: datetime.now(timezone.utc),
    )

    bed = relationship("Bed", back_populates="environment_readings")
