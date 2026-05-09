import importlib
import os
import sqlite3
from pathlib import Path

from alembic import command
from alembic.config import Config
from fastapi.testclient import TestClient


def _prepare_test_db(tmp_path: Path) -> str:
    db_path = tmp_path / "meeting.db"
    db_url = f"sqlite:///{db_path.as_posix()}"
    os.environ["SM_ENV"] = "test"
    os.environ["SM_MEETING_DATABASE_URL"] = db_url
    os.environ["SM_API_PREFIX"] = "/api/v1"
    return db_url


def _run_migrations() -> None:
    here = Path(__file__).resolve().parents[1]
    cfg = Config(str(here / "alembic.ini"))
    cfg.set_main_option("script_location", str(here / "alembic"))
    command.upgrade(cfg, "head")


def _client() -> TestClient:
    mod = importlib.import_module("app")
    importlib.reload(mod)
    return TestClient(mod.app)


def _db_path_from_url(db_url: str) -> str:
    return db_url.replace("sqlite:///", "", 1)


def _audit_count(db_path: str, event_type: str) -> int:
    conn = sqlite3.connect(db_path)
    try:
        row = conn.execute("SELECT COUNT(*) FROM audit_logs WHERE event_type = ?", (event_type,)).fetchone()
        return int(row[0]) if row else 0
    finally:
        conn.close()


def test_meeting_server_auth_and_room_flow(tmp_path: Path):
    db_url = _prepare_test_db(tmp_path)
    db_path = _db_path_from_url(db_url)
    _run_migrations()
    client = _client()

    health = client.get("/health")
    assert health.status_code == 200
    assert health.json()["status"] == "ok"

    register = client.post("/api/v1/auth/register", json={"username": "alice", "password": "alice-pass-123"})
    assert register.status_code == 200

    duplicate_register = client.post("/api/v1/auth/register", json={"username": "alice", "password": "other-pass"})
    assert duplicate_register.status_code == 409
    assert duplicate_register.json()["error"]["code"] == "USERNAME_EXISTS"

    bad_login = client.post("/api/v1/auth/login", json={"username": "alice", "password": "wrong"})
    assert bad_login.status_code == 401
    assert bad_login.json()["error"]["code"] == "INVALID_CREDENTIALS"

    login = client.post("/api/v1/auth/login", json={"username": "alice", "password": "alice-pass-123"})
    assert login.status_code == 200
    login_body = login.json()
    token = login_body["session_token"]
    assert login_body["user_id"] > 0

    create = client.post(
        "/api/v1/rooms/create",
        json={"room_code": "room-100"},
        headers={"x-session-token": token},
    )
    assert create.status_code == 200

    join = client.post(
        "/api/v1/rooms/join",
        json={"room_code": "room-100"},
        headers={"x-session-token": token},
    )
    assert join.status_code == 200
    assert join.json()["status"] in {"joined", "already_joined"}

    owner_policy = client.post(
        "/api/v1/rooms/policy",
        json={"room_code": "room-100", "policy_name": "privacy_mode", "policy_value": "strict"},
        headers={"x-session-token": token},
    )
    assert owner_policy.status_code == 200

    register_bob = client.post("/api/v1/auth/register", json={"username": "bob", "password": "bob-pass-123"})
    assert register_bob.status_code == 200
    login_bob = client.post("/api/v1/auth/login", json={"username": "bob", "password": "bob-pass-123"})
    assert login_bob.status_code == 200
    bob_token = login_bob.json()["session_token"]
    bob_join = client.post(
        "/api/v1/rooms/join",
        json={"room_code": "room-100"},
        headers={"x-session-token": bob_token},
    )
    assert bob_join.status_code == 200
    denied_policy = client.post(
        "/api/v1/rooms/policy",
        json={"room_code": "room-100", "policy_name": "privacy_mode", "policy_value": "open"},
        headers={"x-session-token": bob_token},
    )
    assert denied_policy.status_code == 403
    assert denied_policy.json()["error"]["code"] == "FORBIDDEN"

    signaling = client.post(
        "/api/v1/signaling/message",
        json={"room_code": "room-100", "event_name": "offer", "payload": {"sdp": "placeholder"}},
        headers={"x-session-token": token},
    )
    assert signaling.status_code == 200
    assert signaling.json()["accepted"] is True

    leave = client.post(
        "/api/v1/rooms/leave",
        json={"room_code": "room-100"},
        headers={"x-session-token": token},
    )
    assert leave.status_code == 200
    assert leave.json()["status"] == "left"

    logout = client.post(
        "/api/v1/auth/logout",
        json={},
        headers={"x-session-token": token},
    )
    assert logout.status_code == 200
    assert logout.json()["status"] == "logged_out"

    post_logout = client.post(
        "/api/v1/rooms/join",
        json={"room_code": "room-100"},
        headers={"x-session-token": token},
    )
    assert post_logout.status_code == 401
    assert post_logout.json()["error"]["code"] == "UNAUTHORIZED"

    assert _audit_count(db_path, "user_register") >= 2
    assert _audit_count(db_path, "user_login") >= 2
    assert _audit_count(db_path, "user_logout") >= 1
    assert _audit_count(db_path, "room_created") >= 1
    assert _audit_count(db_path, "room_joined") >= 1
    assert _audit_count(db_path, "room_left") >= 1
    assert _audit_count(db_path, "policy_changed") >= 1
    assert _audit_count(db_path, "authorization_denied") >= 1
    assert _audit_count(db_path, "auth_failed") >= 1


def test_meeting_server_auth_required(tmp_path: Path):
    _prepare_test_db(tmp_path)
    _run_migrations()
    client = _client()
    resp = client.post("/api/v1/rooms/create", json={"room_code": "x"})
    assert resp.status_code == 401
    assert resp.json()["error"]["code"] == "UNAUTHORIZED"
