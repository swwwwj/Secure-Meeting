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


def _client_app():
    mod = importlib.import_module("app")
    importlib.reload(mod)
    return mod.app


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

    face_profile = client.post(
        "/api/v1/face-profiles/me",
        json={"label": "正脸", "image": "ZmFrZS1mYWNlLWltYWdl"},
        headers={"x-session-token": token},
    )
    assert face_profile.status_code == 200
    assert face_profile.json()["status"] == "stored"
    second_face_profile = client.post(
        "/api/v1/face-profiles/me",
        json={"label": "侧脸", "image": "ZmFrZS1mYWNlLWltYWdlLTI="},
        headers={"x-session-token": token},
    )
    assert second_face_profile.status_code == 200

    face_profile_list = client.get(
        "/api/v1/face-profiles",
        headers={"x-session-token": token},
    )
    assert face_profile_list.status_code == 200
    profiles = face_profile_list.json()["profiles"]
    assert [item["profile_key"] for item in profiles] == ["alice::侧脸", "alice::正脸"]

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
    bob_face_query = client.post(
        "/api/v1/face-profiles/query",
        json={"profile_keys": ["alice::正脸", "alice::侧脸", "bob::默认"]},
        headers={"x-session-token": bob_token},
    )
    assert bob_face_query.status_code == 200
    bob_face_profiles = bob_face_query.json()["profiles"]
    assert len(bob_face_profiles) == 2
    assert [item["profile_key"] for item in bob_face_profiles] == ["alice::侧脸", "alice::正脸"]
    assert bob_face_profiles[1]["image"] == "ZmFrZS1mYWNlLWltYWdl"
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
    assert _audit_count(db_path, "face_profile_created") >= 1


def test_meeting_server_auth_required(tmp_path: Path):
    _prepare_test_db(tmp_path)
    _run_migrations()
    client = _client()
    resp = client.post("/api/v1/rooms/create", json={"room_code": "x"})
    assert resp.status_code == 401
    assert resp.json()["error"]["code"] == "UNAUTHORIZED"


def test_startup_applies_pending_migrations_for_face_profiles(tmp_path: Path):
    _prepare_test_db(tmp_path)
    with TestClient(_client_app()) as client:
        register = client.post("/api/v1/auth/register", json={"username": "migrate_user", "password": "migrate-pass-123"})
        assert register.status_code == 200

        login = client.post("/api/v1/auth/login", json={"username": "migrate_user", "password": "migrate-pass-123"})
        assert login.status_code == 200
        token = login.json()["session_token"]

        face_profile = client.post(
            "/api/v1/face-profiles/me",
            json={"label": "默认", "image": "ZmFrZS1mYWNlLWltYWdl"},
            headers={"x-session-token": token},
        )
        assert face_profile.status_code == 200
        assert face_profile.json()["profile_key"] == "migrate_user::默认"
