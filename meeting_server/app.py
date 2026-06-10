import hashlib
import hmac
import json
import logging
import os
import uuid
import base64
from datetime import datetime, timedelta

from fastapi import Depends, FastAPI, Header, Request
from fastapi.responses import JSONResponse
from sqlalchemy import select
from sqlalchemy.orm import Session

from audit import add_audit_event
from config import load_config
from db import SessionLocal
from models import FaceProfile, FaceProfileEntry, Participant, Room, Session as UserSession, User
from schemas import (
    ApiErrorBody,
    CreateRoomRequest,
    FaceProfileDetail,
    FaceProfileEnrollRequest,
    FaceProfileQueryRequest,
    FaceProfileSummary,
    LoginRequest,
    LoginResponse,
    LogoutRequest,
    PolicyChangeRequest,
    RegisterRequest,
    RoomActionRequest,
    SignalingMessageRequest,
)


CONFIG = load_config()
logging.basicConfig(level=logging.INFO, format="%(message)s")
LOGGER = logging.getLogger("securemeeting.meeting_server")
try:
    from passlib.context import CryptContext
    _PWD_CONTEXT = CryptContext(schemes=["bcrypt"], deprecated="auto")
except Exception:  # pragma: no cover
    _PWD_CONTEXT = None


def log_event(event: str, **kwargs: object) -> None:
    payload = {"service": "meeting_server", "event": event, "env": CONFIG.env}
    payload.update(kwargs)
    LOGGER.info(json.dumps(payload, ensure_ascii=True))


class ApiError(Exception):
    def __init__(self, code: str, message: str, status_code: int, request_id: str, trace_id: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.status_code = status_code
        self.request_id = request_id
        self.trace_id = trace_id


def _mask_db_url(db_url: str) -> str:
    if "://" not in db_url or "@" not in db_url:
        return db_url
    scheme, remainder = db_url.split("://", 1)
    credentials, host_part = remainder.split("@", 1)
    if ":" not in credentials:
        return f"{scheme}://***@{host_part}"
    user = credentials.split(":", 1)[0]
    return f"{scheme}://{user}:***@{host_part}"


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def _rid(payload_request_id: str | None) -> str:
    return payload_request_id or str(uuid.uuid4())


def _tid(payload_trace_id: str | None) -> str:
    return payload_trace_id or str(uuid.uuid4())


def hash_password(password: str) -> str:
    if _PWD_CONTEXT is not None:
        return _PWD_CONTEXT.hash(password)
    salt = os.urandom(16).hex()
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt.encode("utf-8"), 120000).hex()
    return f"pbkdf2_sha256${salt}${digest}"


def verify_password(password: str, password_hash: str) -> bool:
    if _PWD_CONTEXT is not None:
        return _PWD_CONTEXT.verify(password, password_hash)
    parts = password_hash.split("$")
    if len(parts) != 3 or parts[0] != "pbkdf2_sha256":
        return False
    _, salt, expected = parts
    got = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt.encode("utf-8"), 120000).hex()
    return hmac.compare_digest(got, expected)


def _iso(dt: datetime) -> str:
    return dt.replace(microsecond=0).isoformat() + "Z"


def _validate_face_image(image_base64: str, request_id: str, trace_id: str) -> str:
    if not image_base64.strip():
        raise ApiError("INVALID_FACE_PROFILE", "Face profile image is required", 400, request_id, trace_id)
    try:
        base64.b64decode(image_base64, validate=True)
    except Exception as exc:
        raise ApiError("INVALID_FACE_PROFILE", f"Invalid face profile image: {exc}", 400, request_id, trace_id) from exc
    return image_base64


def _normalize_face_label(label: str | None) -> str:
    normalized = (label or "").strip()
    return normalized[:64] if normalized else "默认"


def _profile_key(username: str, label: str) -> str:
    return f"{username}::{label}"


def _audit_failure(
    db: Session,
    event_type: str,
    request_id: str,
    trace_id: str,
    detail: dict,
    user_id: int | None = None,
    room_id: int | None = None,
) -> None:
    add_audit_event(db, event_type, user_id, room_id, request_id, trace_id, detail)
    db.commit()


def _resolve_user(
    db: Session,
    token: str | None,
    request_id: str,
    trace_id: str,
    endpoint: str,
) -> User:
    now = datetime.utcnow()
    if not token:
        _audit_failure(
            db,
            "auth_failed",
            request_id,
            trace_id,
            {"reason": "missing_token", "endpoint": endpoint},
        )
        raise ApiError("UNAUTHORIZED", "Missing session token", 401, request_id, trace_id)
    session = db.scalar(select(UserSession).where(UserSession.session_token == token))
    if not session:
        _audit_failure(
            db,
            "auth_failed",
            request_id,
            trace_id,
            {"reason": "invalid_token", "endpoint": endpoint},
        )
        raise ApiError("UNAUTHORIZED", "Invalid session token", 401, request_id, trace_id)
    if session.revoked_at is not None:
        _audit_failure(
            db,
            "auth_failed",
            request_id,
            trace_id,
            {"reason": "revoked_session", "endpoint": endpoint},
            user_id=session.user_id,
        )
        raise ApiError("UNAUTHORIZED", "Session has been revoked", 401, request_id, trace_id)
    if session.expires_at <= now:
        _audit_failure(
            db,
            "auth_failed",
            request_id,
            trace_id,
            {"reason": "expired_session", "endpoint": endpoint},
            user_id=session.user_id,
        )
        raise ApiError("UNAUTHORIZED", "Session has expired", 401, request_id, trace_id)
    user = db.get(User, session.user_id)
    if not user:
        _audit_failure(
            db,
            "auth_failed",
            request_id,
            trace_id,
            {"reason": "missing_user", "endpoint": endpoint},
            user_id=session.user_id,
        )
        raise ApiError("UNAUTHORIZED", "Session user missing", 401, request_id, trace_id)
    return user


app = FastAPI(title="Secure Meeting Server", version="0.1.0")


@app.on_event("startup")
def on_startup() -> None:
    log_event("service_started", database_url=_mask_db_url(CONFIG.database_url))


@app.exception_handler(ApiError)
def api_error_handler(_: Request, exc: ApiError):
    body = ApiErrorBody(code=exc.code, message=exc.message, request_id=exc.request_id, trace_id=exc.trace_id)
    payload = body.model_dump() if hasattr(body, "model_dump") else body.dict()
    log_event("request_failed", code=exc.code, message=exc.message, request_id=exc.request_id, trace_id=exc.trace_id)
    return JSONResponse(status_code=exc.status_code, content={"error": payload})


@app.get("/health")
def health() -> dict:
    return {"status": "ok", "env": CONFIG.env, "enabled": CONFIG.enable_meeting_server}


@app.post(f"{CONFIG.api_prefix}/auth/register")
def register(req: RegisterRequest, db: Session = Depends(get_db)) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    if not CONFIG.enable_meeting_server:
        raise ApiError("MEETING_SERVER_DISABLED", "Meeting server disabled by config", 503, request_id, trace_id)

    exists = db.scalar(select(User).where(User.username == req.username))
    if exists:
        add_audit_event(
            db,
            "register_failed",
            exists.id,
            None,
            request_id,
            trace_id,
            {"reason": "duplicate_username", "username": req.username},
        )
        db.commit()
        raise ApiError("USERNAME_EXISTS", "Username already exists", 409, request_id, trace_id)

    user = User(
        username=req.username,
        password_hash=hash_password(req.password),
        created_at=datetime.utcnow(),
    )
    db.add(user)
    db.flush()
    add_audit_event(db, "user_register", user.id, None, request_id, trace_id, {"username": req.username})
    db.commit()
    return {"request_id": request_id, "trace_id": trace_id, "user_id": user.id, "username": user.username}


@app.post(f"{CONFIG.api_prefix}/auth/login", response_model=LoginResponse)
def login(req: LoginRequest, db: Session = Depends(get_db)) -> LoginResponse:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    if not CONFIG.enable_meeting_server:
        raise ApiError("MEETING_SERVER_DISABLED", "Meeting server disabled by config", 503, request_id, trace_id)

    user = db.scalar(select(User).where(User.username == req.username))
    if not user:
        add_audit_event(
            db,
            "login_failed",
            None,
            None,
            request_id,
            trace_id,
            {"reason": "user_not_found", "username": req.username},
        )
        db.commit()
        raise ApiError("INVALID_CREDENTIALS", "Invalid username or password", 401, request_id, trace_id)
    if not user.password_hash:
        add_audit_event(
            db,
            "login_failed",
            user.id,
            None,
            request_id,
            trace_id,
            {"reason": "password_not_set"},
        )
        db.commit()
        raise ApiError("PASSWORD_NOT_SET", "User password is not configured", 401, request_id, trace_id)
    if not verify_password(req.password, user.password_hash):
        add_audit_event(
            db,
            "login_failed",
            user.id,
            None,
            request_id,
            trace_id,
            {"reason": "wrong_password"},
        )
        db.commit()
        raise ApiError("INVALID_CREDENTIALS", "Invalid username or password", 401, request_id, trace_id)

    token = str(uuid.uuid4())
    session = UserSession(
        session_token=token,
        user_id=user.id,
        created_at=datetime.utcnow(),
        expires_at=datetime.utcnow() + timedelta(hours=8),
        revoked_at=None,
    )
    db.add(session)
    add_audit_event(db, "user_login", user.id, None, request_id, trace_id, {"username": req.username})
    db.commit()
    log_event("login_ok", request_id=request_id, trace_id=trace_id, user_id=user.id)
    return LoginResponse(
        request_id=request_id,
        trace_id=trace_id,
        user_id=user.id,
        username=user.username,
        session_token=token,
    )


@app.post(f"{CONFIG.api_prefix}/auth/logout")
def logout(
    req: LogoutRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    user = _resolve_user(db, x_session_token, request_id, trace_id, "auth/logout")
    session = db.scalar(select(UserSession).where(UserSession.session_token == x_session_token))
    if not session:
        raise ApiError("UNAUTHORIZED", "Invalid session token", 401, request_id, trace_id)
    session.revoked_at = datetime.utcnow()
    add_audit_event(db, "user_logout", user.id, None, request_id, trace_id, {"username": user.username})
    db.commit()
    return {"request_id": request_id, "trace_id": trace_id, "status": "logged_out"}


@app.post(f"{CONFIG.api_prefix}/face-profiles/me")
def upsert_face_profile(
    req: FaceProfileEnrollRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    user = _resolve_user(db, x_session_token, request_id, trace_id, "face-profiles/me")
    image_base64 = _validate_face_image(req.image, request_id, trace_id)
    profile_name = _normalize_face_label(req.label)
    profile = db.scalar(
        select(FaceProfileEntry).where(
            FaceProfileEntry.user_id == user.id,
            FaceProfileEntry.profile_name == profile_name,
        )
    )
    now = datetime.utcnow()
    if profile is None:
        profile = FaceProfileEntry(
            user_id=user.id,
            profile_name=profile_name,
            image_base64=image_base64,
            created_at=now,
            updated_at=now,
        )
        db.add(profile)
        event_type = "face_profile_created"
    else:
        profile.image_base64 = image_base64
        profile.updated_at = now
        event_type = "face_profile_updated"

    add_audit_event(db, event_type, user.id, None, request_id, trace_id, {"username": user.username})
    db.commit()
    return {
        "request_id": request_id,
        "trace_id": trace_id,
        "profile_key": _profile_key(user.username, profile_name),
        "username": user.username,
        "label": profile_name,
        "updated_at": _iso(profile.updated_at),
        "status": "stored",
    }


@app.get(f"{CONFIG.api_prefix}/face-profiles")
def list_face_profiles(
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = str(uuid.uuid4())
    trace_id = str(uuid.uuid4())
    _resolve_user(db, x_session_token, request_id, trace_id, "face-profiles")
    rows = db.execute(
        select(User.username, FaceProfileEntry.profile_name, FaceProfileEntry.updated_at)
        .join(FaceProfileEntry, FaceProfileEntry.user_id == User.id)
        .order_by(User.username.asc(), FaceProfileEntry.profile_name.asc())
    ).all()
    profiles = [
        FaceProfileSummary(profile_key=_profile_key(username, label), username=username, label=label, updated_at=_iso(updated_at)).model_dump()
        if hasattr(FaceProfileSummary, "model_dump")
        else FaceProfileSummary(profile_key=_profile_key(username, label), username=username, label=label, updated_at=_iso(updated_at)).dict()
        for username, label, updated_at in rows
    ]
    return {"request_id": request_id, "trace_id": trace_id, "profiles": profiles}


@app.post(f"{CONFIG.api_prefix}/face-profiles/query")
def query_face_profiles(
    req: FaceProfileQueryRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    _resolve_user(db, x_session_token, request_id, trace_id, "face-profiles/query")
    profile_keys = [value.strip() for value in req.profile_keys if value.strip()]
    names = [name.strip() for name in req.usernames if name.strip()]
    if not profile_keys and not names:
        return {"request_id": request_id, "trace_id": trace_id, "profiles": []}
    rows_query = (
        select(User.username, FaceProfileEntry.profile_name, FaceProfileEntry.image_base64, FaceProfileEntry.updated_at)
        .join(FaceProfileEntry, FaceProfileEntry.user_id == User.id)
    )
    if profile_keys:
        allowed_keys = set(profile_keys)
        rows = [
            row
            for row in db.execute(rows_query.order_by(User.username.asc(), FaceProfileEntry.profile_name.asc())).all()
            if _profile_key(row[0], row[1]) in allowed_keys
        ]
    else:
        rows = db.execute(
            rows_query.where(User.username.in_(names)).order_by(User.username.asc(), FaceProfileEntry.profile_name.asc())
        ).all()
    profiles = [
        FaceProfileDetail(
            profile_key=_profile_key(username, label),
            username=username,
            label=label,
            image=image,
            updated_at=_iso(updated_at),
        ).model_dump()
        if hasattr(FaceProfileDetail, "model_dump")
        else FaceProfileDetail(
            profile_key=_profile_key(username, label),
            username=username,
            label=label,
            image=image,
            updated_at=_iso(updated_at),
        ).dict()
        for username, label, image, updated_at in rows
    ]
    return {"request_id": request_id, "trace_id": trace_id, "profiles": profiles}


@app.post(f"{CONFIG.api_prefix}/rooms/create")
def create_room(
    req: CreateRoomRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    user = _resolve_user(db, x_session_token, request_id, trace_id, "rooms/create")
    exists = db.scalar(select(Room).where(Room.room_code == req.room_code))
    if exists:
        raise ApiError("ROOM_EXISTS", "Room code already exists", 409, request_id, trace_id)

    room = Room(room_code=req.room_code, owner_user_id=user.id, active=True, created_at=datetime.utcnow())
    db.add(room)
    db.flush()
    db.add(Participant(room_id=room.id, user_id=user.id, role="owner", joined_at=datetime.utcnow()))
    add_audit_event(db, "room_created", user.id, room.id, request_id, trace_id, {"room_code": room.room_code})
    db.commit()
    return {"request_id": request_id, "trace_id": trace_id, "room_code": room.room_code}


@app.post(f"{CONFIG.api_prefix}/rooms/join")
def join_room(
    req: RoomActionRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    user = _resolve_user(db, x_session_token, request_id, trace_id, "rooms/join")
    room = db.scalar(select(Room).where(Room.room_code == req.room_code, Room.active.is_(True)))
    if not room:
        raise ApiError("ROOM_NOT_FOUND", "Room not found", 404, request_id, trace_id)

    participant = db.scalar(select(Participant).where(Participant.room_id == room.id, Participant.user_id == user.id))
    if participant and participant.left_at is None:
        return {"request_id": request_id, "trace_id": trace_id, "room_code": room.room_code, "status": "already_joined"}
    if participant and participant.left_at is not None:
        participant.left_at = None
        participant.joined_at = datetime.utcnow()
    if not participant:
        db.add(Participant(room_id=room.id, user_id=user.id, role="member", joined_at=datetime.utcnow()))
    add_audit_event(db, "room_joined", user.id, room.id, request_id, trace_id, {"room_code": room.room_code})
    db.commit()
    return {"request_id": request_id, "trace_id": trace_id, "room_code": room.room_code, "status": "joined"}


@app.post(f"{CONFIG.api_prefix}/rooms/leave")
def leave_room(
    req: RoomActionRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    user = _resolve_user(db, x_session_token, request_id, trace_id, "rooms/leave")
    room = db.scalar(select(Room).where(Room.room_code == req.room_code, Room.active.is_(True)))
    if not room:
        raise ApiError("ROOM_NOT_FOUND", "Room not found", 404, request_id, trace_id)
    participant = db.scalar(select(Participant).where(Participant.room_id == room.id, Participant.user_id == user.id))
    if not participant or participant.left_at is not None:
        raise ApiError("NOT_IN_ROOM", "User is not in room", 400, request_id, trace_id)
    participant.left_at = datetime.utcnow()
    add_audit_event(db, "room_left", user.id, room.id, request_id, trace_id, {"room_code": room.room_code})
    db.commit()
    return {"request_id": request_id, "trace_id": trace_id, "room_code": room.room_code, "status": "left"}


@app.post(f"{CONFIG.api_prefix}/signaling/message")
def signaling_message(
    req: SignalingMessageRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    user = _resolve_user(db, x_session_token, request_id, trace_id, "signaling/message")
    if not CONFIG.enable_signaling_placeholder:
        raise ApiError("SIGNALING_DISABLED", "Signaling placeholder disabled by config", 503, request_id, trace_id)
    room = db.scalar(select(Room).where(Room.room_code == req.room_code, Room.active.is_(True)))
    if not room:
        raise ApiError("ROOM_NOT_FOUND", "Room not found", 404, request_id, trace_id)
    add_audit_event(
        db,
        "signaling_message",
        user.id,
        room.id,
        request_id,
        trace_id,
        {"event_name": req.event_name, "payload": req.payload},
    )
    db.commit()
    return {"request_id": request_id, "trace_id": trace_id, "accepted": True, "event_name": req.event_name}


@app.post(f"{CONFIG.api_prefix}/rooms/policy")
def change_policy(
    req: PolicyChangeRequest,
    db: Session = Depends(get_db),
    x_session_token: str | None = Header(default=None),
) -> dict:
    request_id = _rid(req.request_id)
    trace_id = _tid(req.trace_id)
    user = _resolve_user(db, x_session_token, request_id, trace_id, "rooms/policy")
    room = db.scalar(select(Room).where(Room.room_code == req.room_code, Room.active.is_(True)))
    if not room:
        raise ApiError("ROOM_NOT_FOUND", "Room not found", 404, request_id, trace_id)
    if room.owner_user_id != user.id:
        add_audit_event(
            db,
            "authorization_denied",
            user.id,
            room.id,
            request_id,
            trace_id,
            {"reason": "owner_required", "endpoint": "rooms/policy"},
        )
        db.commit()
        raise ApiError("FORBIDDEN", "Only room owner can change policy", 403, request_id, trace_id)
    add_audit_event(
        db,
        "policy_changed",
        user.id,
        room.id,
        request_id,
        trace_id,
        {"policy_name": req.policy_name, "policy_value": req.policy_value},
    )
    db.commit()
    return {"request_id": request_id, "trace_id": trace_id, "status": "accepted"}
