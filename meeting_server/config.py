import importlib.util
import json
import logging
import os
from pathlib import Path
from typing import Literal

from pydantic import BaseModel


class ServerConfig(BaseModel):
    env: Literal["dev", "test", "prod"] = "dev"
    host: str = "127.0.0.1"
    port: int = 8100
    api_prefix: str = "/api/v1"
    database_url: str = "postgresql+psycopg://postgres:postgres@127.0.0.1:5432/secure_meeting"
    enable_meeting_server: bool = True
    enable_signaling_placeholder: bool = True


LOGGER = logging.getLogger("securemeeting.meeting_server.config")
logging.basicConfig(level=logging.INFO, format="%(message)s")


def _mask_db_url(db_url: str) -> str:
    if "://" not in db_url or "@" not in db_url:
        return db_url
    scheme, remainder = db_url.split("://", 1)
    credentials, host_part = remainder.split("@", 1)
    if ":" not in credentials:
        return f"{scheme}://***@{host_part}"
    user = credentials.split(":", 1)[0]
    return f"{scheme}://{user}:***@{host_part}"


def _is_postgres_url(db_url: str) -> bool:
    lower = db_url.lower()
    return lower.startswith("postgresql://") or lower.startswith("postgresql+")


def _postgres_driver_available() -> bool:
    return (
        importlib.util.find_spec("psycopg") is not None
        or importlib.util.find_spec("psycopg2") is not None
    )


def load_config() -> ServerConfig:
    env = os.getenv("SM_ENV", "dev")
    base_dir = Path(__file__).resolve().parent
    cfg_path = base_dir / "config" / f"{env}.json"
    data = {}
    if cfg_path.exists():
        data = json.loads(cfg_path.read_text(encoding="utf-8"))
    data["env"] = env

    if os.getenv("SM_MEETING_DATABASE_URL"):
        data["database_url"] = os.getenv("SM_MEETING_DATABASE_URL")
    if os.getenv("SM_API_PREFIX"):
        data["api_prefix"] = os.getenv("SM_API_PREFIX")
    if os.getenv("SM_ENABLE_MEETING_SERVER"):
        data["enable_meeting_server"] = os.getenv("SM_ENABLE_MEETING_SERVER").lower() == "true"
    config = ServerConfig(**data)

    fallback_reason = ""
    fallback_applied = False
    if _is_postgres_url(config.database_url) and not _postgres_driver_available():
        fallback_reason = "postgres driver not installed"
        if config.env == "dev":
            config.database_url = "sqlite:///./meeting_server_dev.db"
            fallback_applied = True

    LOGGER.info(
        json.dumps(
            {
                "service": "meeting_server",
                "event": "config_loaded",
                "env": config.env,
                "database_url": _mask_db_url(config.database_url),
                "fallback_applied": fallback_applied,
                "fallback_reason": fallback_reason,
            },
            ensure_ascii=True,
        )
    )
    if fallback_reason and not fallback_applied:
        error_payload = {
            "service": "meeting_server",
            "event": "config_error",
            "env": config.env,
            "message": "PostgreSQL driver is unavailable and fallback is only enabled in dev environment.",
            "database_url": _mask_db_url(config.database_url),
        }
        LOGGER.error(json.dumps(error_payload, ensure_ascii=True))
        raise RuntimeError(error_payload["message"])

    return config
