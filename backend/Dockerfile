# =========================================================
# VitalKare Backend — Dockerfile
# Imagem multi-stage para produção
# =========================================================

# --- Stage 1: Builder ---
FROM python:3.11-slim AS builder

WORKDIR /build

# Instala dependências de sistema necessárias para compilar pacotes
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --user --no-cache-dir -r requirements.txt


# --- Stage 2: Runtime ---
FROM python:3.11-slim AS runtime

# Variáveis de build
ARG APP_USER=vitalkare
ARG APP_HOME=/app

# Usuário não-privilegiado
RUN useradd --create-home --shell /bin/bash $APP_USER

# Dependências de runtime apenas
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    && rm -rf /var/lib/apt/lists/*

# Copia pacotes instalados do stage builder
COPY --from=builder /root/.local /home/$APP_USER/.local

WORKDIR $APP_HOME

COPY --chown=$APP_USER:$APP_USER . .

USER $APP_USER

ENV PATH="/home/$APP_USER/.local/bin:$PATH"
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1

EXPOSE 8000

# Health check para Docker Compose / Kubernetes
HEALTHCHECK --interval=30s --timeout=10s --start-period=15s --retries=3 \
    CMD python -c "import httpx; httpx.get('http://localhost:8000/health').raise_for_status()"

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "2"]
