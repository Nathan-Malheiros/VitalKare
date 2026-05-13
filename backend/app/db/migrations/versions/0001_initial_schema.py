"""Schema inicial completo do VitalKare

Revision ID: 0001
Revises: 
Create Date: 2026-05-13 00:00:00.000000
"""
from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "0001"
down_revision: Union[str, None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    # =========================================================
    # Extensões
    # =========================================================
    op.execute('CREATE EXTENSION IF NOT EXISTS "uuid-ossp"')

    # TimescaleDB pode não estar disponível em todos os ambientes;
    # usamos uma verificação segura.
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM pg_extension WHERE extname = 'timescaledb'
            ) THEN
                BEGIN
                    CREATE EXTENSION timescaledb CASCADE;
                EXCEPTION WHEN OTHERS THEN
                    RAISE NOTICE 'timescaledb não disponível, pulando...';
                END;
            END IF;
        END $$;
    """)

    # =========================================================
    # wards
    # =========================================================
    op.create_table(
        "wards",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("name", sa.String(100), nullable=False),
        sa.Column("floor", sa.Integer(), nullable=True),
        sa.Column("capacity", sa.Integer(), nullable=False, server_default="10"),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("name"),
    )

    # =========================================================
    # beds
    # =========================================================
    op.create_table(
        "beds",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("ward_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("bed_number", sa.String(20), nullable=False),
        sa.Column("device_id", sa.String(50), nullable=False),
        sa.Column("status", sa.String(20), nullable=False, server_default="OFFLINE"),
        sa.Column("last_heartbeat", sa.DateTime(timezone=True), nullable=True),
        sa.Column("firmware_version", sa.String(20), nullable=True),
        sa.Column("wifi_rssi", sa.Integer(), nullable=True),
        sa.Column("free_heap_bytes", sa.Integer(), nullable=True),
        sa.Column("metadata_json", postgresql.JSONB(), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.ForeignKeyConstraint(["ward_id"], ["wards.id"], ondelete="RESTRICT"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("bed_number"),
        sa.UniqueConstraint("device_id"),
    )
    op.create_index("idx_beds_ward_id",   "beds", ["ward_id"])
    op.create_index("idx_beds_status",    "beds", ["status"])
    op.create_index("idx_beds_device_id", "beds", ["device_id"])

    # =========================================================
    # patients
    # =========================================================
    op.create_table(
        "patients",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("name", sa.String(200), nullable=False),
        sa.Column("birth_date", sa.Date(), nullable=True),
        sa.Column("medical_record_number", sa.String(50), nullable=True),
        sa.Column("admission_date", sa.DateTime(timezone=True), nullable=True),
        sa.Column("discharge_date", sa.DateTime(timezone=True), nullable=True),
        sa.Column("notes", sa.Text(), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("medical_record_number"),
    )

    # =========================================================
    # bed_assignments
    # =========================================================
    op.create_table(
        "bed_assignments",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("bed_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("patient_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("assigned_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("released_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("is_active", sa.Boolean(), nullable=False, server_default="true"),
        sa.ForeignKeyConstraint(["bed_id"], ["beds.id"], ondelete="RESTRICT"),
        sa.ForeignKeyConstraint(["patient_id"], ["patients.id"], ondelete="RESTRICT"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("idx_ba_bed_id",     "bed_assignments", ["bed_id"])
    op.create_index("idx_ba_patient_id", "bed_assignments", ["patient_id"])
    op.execute(
        "CREATE INDEX idx_ba_active ON bed_assignments(is_active) WHERE is_active = TRUE"
    )

    # =========================================================
    # operators
    # =========================================================
    op.create_table(
        "operators",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("name", sa.String(200), nullable=False),
        sa.Column("email", sa.String(200), nullable=False),
        sa.Column("role", sa.String(20), nullable=False, server_default="VIEWER"),
        sa.Column("password_hash", sa.String(200), nullable=False),
        sa.Column("is_active", sa.Boolean(), nullable=False, server_default="true"),
        sa.Column("last_login", sa.DateTime(timezone=True), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("email"),
    )

    # =========================================================
    # events
    # =========================================================
    op.create_table(
        "events",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("bed_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("event_type", sa.String(20), nullable=False),
        sa.Column("priority", sa.Integer(), nullable=False, server_default="5"),
        sa.Column("status", sa.String(20), nullable=False, server_default="PENDING"),
        sa.Column("payload", postgresql.JSONB(), nullable=True),
        sa.Column("source_topic", sa.String(200), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("resolved_at", sa.DateTime(timezone=True), nullable=True),
        sa.ForeignKeyConstraint(["bed_id"], ["beds.id"], ondelete="RESTRICT"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("idx_events_bed_id",      "events", ["bed_id"])
    op.create_index("idx_events_status",      "events", ["status"])
    op.create_index("idx_events_priority",    "events", ["priority"])
    op.execute(
        "CREATE INDEX idx_events_created_at ON events(created_at DESC)"
    )
    op.create_index("idx_events_type_status", "events", ["event_type", "status"])

    # =========================================================
    # acknowledgements
    # =========================================================
    op.create_table(
        "acknowledgements",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("event_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("operator_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("ack_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("response_time_seconds", sa.Integer(), nullable=True),
        sa.Column("notes", sa.String(1000), nullable=True),
        sa.ForeignKeyConstraint(["event_id"],   ["events.id"],    ondelete="CASCADE"),
        sa.ForeignKeyConstraint(["operator_id"], ["operators.id"], ondelete="RESTRICT"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("idx_ack_event_id",    "acknowledgements", ["event_id"])
    op.create_index("idx_ack_operator_id", "acknowledgements", ["operator_id"])

    # =========================================================
    # audio_records
    # =========================================================
    op.create_table(
        "audio_records",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("event_id", postgresql.UUID(as_uuid=True), nullable=True),
        sa.Column("bed_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("file_path", sa.String(500), nullable=True),
        sa.Column("duration_seconds", sa.Float(), nullable=True),
        sa.Column("sample_rate", sa.Integer(), server_default="16000", nullable=True),
        sa.Column("file_size_bytes", sa.BigInteger(), nullable=True),
        sa.Column("checksum_sha256", sa.String(64), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.ForeignKeyConstraint(["bed_id"],   ["beds.id"],   ondelete="RESTRICT"),
        sa.ForeignKeyConstraint(["event_id"], ["events.id"], ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("idx_audio_bed_id", "audio_records", ["bed_id"])

    # =========================================================
    # transcriptions
    # =========================================================
    op.create_table(
        "transcriptions",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("audio_record_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("text", sa.Text(), nullable=True),
        sa.Column("confidence", sa.Float(), nullable=True),
        sa.Column("language", sa.String(10), server_default="pt", nullable=True),
        sa.Column("model_used", sa.String(100), nullable=True),
        sa.Column("processing_time_ms", sa.Integer(), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.ForeignKeyConstraint(["audio_record_id"], ["audio_records.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )

    # =========================================================
    # environment_readings (time-series)
    # =========================================================
    op.create_table(
        "environment_readings",
        sa.Column("id", postgresql.UUID(as_uuid=True), server_default=sa.text("uuid_generate_v4()"), nullable=False),
        sa.Column("bed_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("temperature_celsius", sa.Float(), nullable=True),
        sa.Column("humidity_percent", sa.Float(), nullable=True),
        sa.Column("luminosity_lux", sa.Float(), nullable=True),
        sa.Column("noise_level_db", sa.Float(), nullable=True),
        sa.Column("motion_detected", sa.Boolean(), server_default="false", nullable=True),
        sa.Column("recorded_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.ForeignKeyConstraint(["bed_id"], ["beds.id"], ondelete="RESTRICT"),
        sa.PrimaryKeyConstraint("id", "recorded_at"),
    )
    op.execute(
        "CREATE INDEX idx_env_bed_id ON environment_readings(bed_id, recorded_at DESC)"
    )

    # Converter para hipertabela TimescaleDB se a extensão estiver disponível
    op.execute("""
        DO $$
        BEGIN
            IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'timescaledb') THEN
                PERFORM create_hypertable(
                    'environment_readings',
                    'recorded_at',
                    if_not_exists => TRUE
                );
            END IF;
        END $$;
    """)


def downgrade() -> None:
    op.drop_table("environment_readings")
    op.drop_table("transcriptions")
    op.drop_table("audio_records")
    op.drop_table("acknowledgements")
    op.drop_table("events")
    op.drop_table("operators")
    op.drop_table("bed_assignments")
    op.drop_table("patients")
    op.drop_table("beds")
    op.drop_table("wards")
