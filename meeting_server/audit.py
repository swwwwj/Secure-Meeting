import json
from datetime import datetime

from sqlalchemy.orm import Session

from models import AuditLog


def add_audit_event(
    db: Session,
    event_type: str,
    user_id: int | None,
    room_id: int | None,
    request_id: str,
    trace_id: str,
    detail: dict,
) -> None:
    db.add(
        AuditLog(
            event_type=event_type,
            user_id=user_id,
            room_id=room_id,
            request_id=request_id,
            trace_id=trace_id,
            detail_json=json.dumps(detail, ensure_ascii=True),
            created_at=datetime.utcnow(),
        )
    )
