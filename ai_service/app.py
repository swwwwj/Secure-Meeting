import base64
import json
import logging
import os
import time
import uuid
from pathlib import Path
from typing import Literal, Optional

import cv2
import numpy as np
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field


class ServiceConfig(BaseModel):
    env: Literal["dev", "test", "prod"] = "dev"
    host: str = "127.0.0.1"
    port: int = 8000
    api_prefix: str = "/api/v1"
    default_mode: Literal["pass", "grayscale"] = "grayscale"
    model_version: str = "demo-model-v1"
    policy_version: str = "policy-default-v1"
    enable_metrics_log: bool = True
    metrics_log_every_n: int = 30
    enable_debug_controls: bool = True


def _load_config() -> ServiceConfig:
    env = os.getenv("SM_ENV", "dev")
    base_dir = Path(__file__).resolve().parent
    cfg_path = base_dir / "config" / f"{env}.json"
    data = {}
    if cfg_path.exists():
        data = json.loads(cfg_path.read_text(encoding="utf-8"))
    data["env"] = env
    if os.getenv("SM_MODEL_VERSION"):
        data["model_version"] = os.getenv("SM_MODEL_VERSION")
    if os.getenv("SM_POLICY_VERSION"):
        data["policy_version"] = os.getenv("SM_POLICY_VERSION")
    if os.getenv("SM_API_PREFIX"):
        data["api_prefix"] = os.getenv("SM_API_PREFIX")
    return ServiceConfig(**data)


CONFIG = _load_config()
logging.basicConfig(level=logging.INFO, format="%(message)s")
LOGGER = logging.getLogger("securemeeting.ai_service")


def log_event(event: str, **kwargs: object) -> None:
    payload = {"ts_ms": int(time.time() * 1000), "service": "ai_service", "event": event, "env": CONFIG.env}
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


class DebugOptions(BaseModel):
    simulate_delay_ms: Optional[int] = Field(default=None, ge=0, le=10000)
    force_error: bool = False


class ProcessFrameRequest(BaseModel):
    image: str = Field(..., description="Base64 encoded image bytes")
    mode: Literal["pass", "grayscale"] = Field(default=CONFIG.default_mode)
    request_id: Optional[str] = None
    trace_id: Optional[str] = None
    model_version: Optional[str] = None
    policy_version: Optional[str] = None
    debug: Optional[DebugOptions] = None


class ErrorBody(BaseModel):
    code: str
    message: str
    request_id: str
    trace_id: str


class ProcessFrameResponse(BaseModel):
    request_id: str
    trace_id: str
    model_version: str
    policy_version: str
    image: str
    latency_ms: float


class Metrics:
    def __init__(self) -> None:
        self.start_ts = time.monotonic()
        self.total_requests = 0
        self.success = 0
        self.failed = 0
        self.total_latency_ms = 0.0
        self.total_inference_ms = 0.0

    def record(self, ok: bool, latency_ms: float, inference_ms: float) -> None:
        self.total_requests += 1
        if ok:
            self.success += 1
        else:
            self.failed += 1
        self.total_latency_ms += latency_ms
        self.total_inference_ms += inference_ms

    def snapshot(self) -> dict:
        elapsed = max(time.monotonic() - self.start_ts, 1e-6)
        failure_rate = self.failed / self.total_requests if self.total_requests else 0.0
        return {
            "uptime_sec": elapsed,
            "requests_total": self.total_requests,
            "success_total": self.success,
            "failed_total": self.failed,
            "failure_rate": failure_rate,
            "throughput_fps": self.total_requests / elapsed,
            "avg_latency_ms": self.total_latency_ms / self.total_requests if self.total_requests else 0.0,
            "avg_inference_ms": self.total_inference_ms / self.total_requests if self.total_requests else 0.0,
        }


METRICS = Metrics()
app = FastAPI(title="Secure Meeting AI Service", version="1.0.0")


@app.exception_handler(ApiError)
def api_error_handler(_: Request, exc: ApiError) -> JSONResponse:
    log_event(
        "request_failed",
        code=exc.code,
        message=exc.message,
        request_id=exc.request_id,
        trace_id=exc.trace_id,
    )
    error_body = ErrorBody(code=exc.code, message=exc.message, request_id=exc.request_id, trace_id=exc.trace_id)
    payload = error_body.model_dump() if hasattr(error_body, "model_dump") else error_body.dict()
    return JSONResponse(
        status_code=exc.status_code,
        content={"error": payload},
    )


def decode_base64_image(image_base64: str, request_id: str, trace_id: str) -> np.ndarray:
    try:
        raw = base64.b64decode(image_base64)
        np_buffer = np.frombuffer(raw, dtype=np.uint8)
        image = cv2.imdecode(np_buffer, cv2.IMREAD_COLOR)
    except Exception as exc:  # pragma: no cover
        raise ApiError("INVALID_IMAGE_PAYLOAD", f"Invalid image payload: {exc}", 400, request_id, trace_id) from exc

    if image is None:
        raise ApiError("IMAGE_DECODE_FAILED", "Image decode failed", 400, request_id, trace_id)
    return image


def encode_base64_image(image: np.ndarray, request_id: str, trace_id: str) -> str:
    ok, encoded = cv2.imencode(".png", image)
    if not ok:
        raise ApiError("IMAGE_ENCODE_FAILED", "Image encode failed", 500, request_id, trace_id)
    return base64.b64encode(encoded.tobytes()).decode("utf-8")


def _resolve_meta(req: ProcessFrameRequest) -> tuple[str, str, str, str]:
    request_id = req.request_id or str(uuid.uuid4())
    trace_id = req.trace_id or str(uuid.uuid4())
    model_version = req.model_version or CONFIG.model_version
    policy_version = req.policy_version or CONFIG.policy_version
    return request_id, trace_id, model_version, policy_version


def _process_impl(req: ProcessFrameRequest) -> ProcessFrameResponse:
    request_id, trace_id, model_version, policy_version = _resolve_meta(req)
    start = time.perf_counter()

    if req.debug and not CONFIG.enable_debug_controls:
        raise ApiError("DEBUG_CONTROLS_DISABLED", "Debug controls are disabled in this environment", 403, request_id, trace_id)
    if req.debug and req.debug.force_error:
        raise ApiError("DEBUG_FORCED_ERROR", "Forced error for test", 500, request_id, trace_id)
    if req.debug and req.debug.simulate_delay_ms:
        time.sleep(req.debug.simulate_delay_ms / 1000.0)

    image = decode_base64_image(req.image, request_id, trace_id)
    infer_start = time.perf_counter()
    if req.mode == "grayscale":
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        processed = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    else:
        processed = image
    inference_ms = (time.perf_counter() - infer_start) * 1000.0

    out = ProcessFrameResponse(
        request_id=request_id,
        trace_id=trace_id,
        model_version=model_version,
        policy_version=policy_version,
        image=encode_base64_image(processed, request_id, trace_id),
        latency_ms=(time.perf_counter() - start) * 1000.0,
    )
    METRICS.record(ok=True, latency_ms=out.latency_ms, inference_ms=inference_ms)
    if CONFIG.enable_metrics_log and METRICS.total_requests % max(CONFIG.metrics_log_every_n, 1) == 0:
        log_event("metrics_snapshot", **METRICS.snapshot())
    log_event(
        "process_frame_ok",
        request_id=request_id,
        trace_id=trace_id,
        model_version=model_version,
        policy_version=policy_version,
        latency_ms=out.latency_ms,
        inference_ms=inference_ms,
    )
    return out


@app.get("/health")
def health() -> dict:
    return {"status": "ok", "env": CONFIG.env}


@app.get(f"{CONFIG.api_prefix}/metrics")
def metrics() -> dict:
    return METRICS.snapshot()


@app.post(f"{CONFIG.api_prefix}/process_frame", response_model=ProcessFrameResponse)
def process_frame_v1(req: ProcessFrameRequest) -> ProcessFrameResponse:
    try:
        return _process_impl(req)
    except ApiError:
        METRICS.record(ok=False, latency_ms=0.0, inference_ms=0.0)
        raise


@app.post("/process_frame")
def process_frame_legacy(req: ProcessFrameRequest) -> dict:
    """
    Backward-compatible endpoint for old demo clients.
    Legacy shape remains {"image": "..."} while internally using v1 contract.
    """
    out = process_frame_v1(req)
    return {"image": out.image}
