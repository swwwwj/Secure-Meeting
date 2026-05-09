from pydantic import BaseModel, Field


class RegisterRequest(BaseModel):
    username: str = Field(min_length=1, max_length=64)
    password: str = Field(min_length=6, max_length=128)
    request_id: str | None = None
    trace_id: str | None = None


class LoginRequest(BaseModel):
    username: str = Field(min_length=1, max_length=64)
    password: str = Field(min_length=1, max_length=128)
    request_id: str | None = None
    trace_id: str | None = None


class LoginResponse(BaseModel):
    request_id: str
    trace_id: str
    user_id: int
    username: str
    session_token: str


class LogoutRequest(BaseModel):
    request_id: str | None = None
    trace_id: str | None = None


class CreateRoomRequest(BaseModel):
    room_code: str = Field(min_length=1, max_length=64)
    request_id: str | None = None
    trace_id: str | None = None


class RoomActionRequest(BaseModel):
    room_code: str
    request_id: str | None = None
    trace_id: str | None = None


class SignalingMessageRequest(BaseModel):
    room_code: str
    event_name: str = Field(min_length=1, max_length=64)
    payload: dict = Field(default_factory=dict)
    request_id: str | None = None
    trace_id: str | None = None


class PolicyChangeRequest(BaseModel):
    room_code: str
    policy_name: str = Field(min_length=1, max_length=64)
    policy_value: str = Field(min_length=1, max_length=128)
    request_id: str | None = None
    trace_id: str | None = None


class ApiErrorBody(BaseModel):
    code: str
    message: str
    request_id: str
    trace_id: str
