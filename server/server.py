"""
VitalKare MVP Server — entry point.

Starts two concurrent servers in the same asyncio event loop:
  1. TCP server  on 0.0.0.0:7890  — VK binary protocol (ESP32 devices)
  2. HTTP server on 0.0.0.0:8000  — FastAPI REST + WebSocket (dashboard)

Usage:
  pip install -r requirements.txt
  python server.py

Environment variables:
  VK_TCP_HOST   (default: 0.0.0.0)
  VK_TCP_PORT   (default: 7890)
  VK_HTTP_HOST  (default: 0.0.0.0)
  VK_HTTP_PORT  (default: 8000)
  VK_JWT_SECRET (default: vitalkare-mvp-secret-2024)
"""

from __future__ import annotations
import asyncio
import logging
import os
import sys

import uvicorn

import db
import tcp_handler
from api import app

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("vitalkare.main")

TCP_HOST = os.environ.get("VK_TCP_HOST", "0.0.0.0")
TCP_PORT = int(os.environ.get("VK_TCP_PORT", "7890"))
HTTP_HOST = os.environ.get("VK_HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(os.environ.get("VK_HTTP_PORT", "8000"))


async def _tcp_server() -> None:
    server = await asyncio.start_server(
        tcp_handler.handle_connection,
        host=TCP_HOST,
        port=TCP_PORT,
    )
    addrs = ", ".join(str(s.getsockname()) for s in server.sockets)
    logger.info("VK TCP server listening on %s", addrs)
    async with server:
        await server.serve_forever()


async def main() -> None:
    # Initialise DB (create tables + seed admin)
    await db.init_db()
    logger.info("SQLite DB ready at %s", db.DB_PATH)

    # uvicorn config — log_config=None keeps our root logger active
    config = uvicorn.Config(
        app,
        host=HTTP_HOST,
        port=HTTP_PORT,
        log_config=None,
        loop="none",  # we manage the event loop
    )
    server = uvicorn.Server(config)

    logger.info("HTTP / WebSocket server starting on http://%s:%d", HTTP_HOST, HTTP_PORT)
    logger.info("Dashboard:  http://localhost:5173  (run: cd dashboard && npm run dev)")
    logger.info("API docs:   http://localhost:%d/docs", HTTP_PORT)
    logger.info("Admin login: admin / admin")
    logger.info("")

    # Run TCP server and HTTP server concurrently
    await asyncio.gather(
        _tcp_server(),
        server.serve(),
    )


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Server stopped by user.")
        sys.exit(0)
