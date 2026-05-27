import base64
import json
import logging
import os
import time
import uuid
from pathlib import Path
from typing import Any, Literal, Optional

import cv2
import numpy as np
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from face_pipeline import (
    FaceGallery,
    FacePrivacyPipeline,
    InsightFaceRecognizer,
    MockFaceRecognizer,
)
from pipeline import MockDetector, RegionProtector, SensitiveObjectPipeline, UnavailableDetector, YOLODetector


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
    detector_provider: Literal["mock", "yolo"] = "mock"
    detector_device: str = "cpu"
    yolo_model_path: str = "yolov8n.pt"
    detection_confidence_threshold: float = 0.35
    inference_image_size: int = 640
    detector_timeout_ms: int = 200
    detect_every_n_frames: int = 1
    bbox_smoothing_alpha: float = 0.7
    blur_method: Literal["gaussian", "mosaic"] = "gaussian"
    blur_intensity: int = 17
    sensitive_labels: list[str] = Field(default_factory=list)
    face_provider: Literal["mock", "insightface"] = "mock"
    arcface_model_dir: str = "models/arcface"
    face_match_threshold: float = 0.45
    face_detect_every_n_frames: int = 2
    face_model_version: str = "arcface-phase-d-v1"


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
    if os.getenv("SM_DETECTOR_PROVIDER"):
        data["detector_provider"] = os.getenv("SM_DETECTOR_PROVIDER")
    if os.getenv("SM_DETECTOR_DEVICE"):
        data["detector_device"] = os.getenv("SM_DETECTOR_DEVICE")
    if os.getenv("SM_YOLO_MODEL_PATH"):
        data["yolo_model_path"] = os.getenv("SM_YOLO_MODEL_PATH")
    if os.getenv("SM_SENSITIVE_LABELS"):
        try:
            data["sensitive_labels"] = json.loads(os.getenv("SM_SENSITIVE_LABELS", "[]"))
        except Exception:
            data["sensitive_labels"] = []
    if os.getenv("SM_FACE_PROVIDER"):
        data["face_provider"] = os.getenv("SM_FACE_PROVIDER")
    if os.getenv("SM_ARCFACE_MODEL_DIR"):
        data["arcface_model_dir"] = os.getenv("SM_ARCFACE_MODEL_DIR")
    if os.getenv("SM_FACE_MATCH_THRESHOLD"):
        data["face_match_threshold"] = float(os.getenv("SM_FACE_MATCH_THRESHOLD", "0.45"))
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
    room_id: Optional[str] = None
    whitelist_user_ids: list[str] = Field(default_factory=list)
    enable_face_privacy: bool = False
    enable_object_detection: Optional[bool] = None
    debug: Optional[DebugOptions] = None


class FaceEnrollRequest(BaseModel):
    room_id: str
    user_id: str
    image: str
    request_id: Optional[str] = None
    trace_id: Optional[str] = None


class FaceClearRequest(BaseModel):
    room_id: str
    request_id: Optional[str] = None
    trace_id: Optional[str] = None


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
    detections: Optional[list[dict[str, Any]]] = None
    blurred_count: Optional[int] = None
    faces: Optional[list[dict[str, Any]]] = None
    faces_detected: Optional[int] = None
    faces_blurred: Optional[int] = None


class Metrics:
    def __init__(self) -> None:
        self.start_ts = time.monotonic()
        self.total_requests = 0
        self.success = 0
        self.failed = 0
        self.total_latency_ms = 0.0
        self.total_inference_ms = 0.0
        self.total_detection_ms = 0.0
        self.total_detected_objects = 0
        self.total_blurred_objects = 0
        self.total_skipped_detection = 0
        self.total_degraded = 0
        self.total_faces_detected = 0
        self.total_faces_blurred = 0

    def record(
        self,
        ok: bool,
        latency_ms: float,
        inference_ms: float,
        detection_ms: float = 0.0,
        detect_count: int = 0,
        blurred_count: int = 0,
        skipped_detection: bool = False,
        degraded: bool = False,
        faces_detected: int = 0,
        faces_blurred: int = 0,
    ) -> None:
        self.total_requests += 1
        if ok:
            self.success += 1
        else:
            self.failed += 1
        self.total_latency_ms += latency_ms
        self.total_inference_ms += inference_ms
        self.total_detection_ms += detection_ms
        self.total_detected_objects += detect_count
        self.total_blurred_objects += blurred_count
        if skipped_detection:
            self.total_skipped_detection += 1
        if degraded:
            self.total_degraded += 1
        self.total_faces_detected += faces_detected
        self.total_faces_blurred += faces_blurred

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
            "avg_detection_ms": self.total_detection_ms / self.total_requests if self.total_requests else 0.0,
            "detected_objects_total": self.total_detected_objects,
            "blurred_objects_total": self.total_blurred_objects,
            "skipped_detection_total": self.total_skipped_detection,
            "degraded_total": self.total_degraded,
            "faces_detected_total": self.total_faces_detected,
            "faces_blurred_total": self.total_faces_blurred,
        }


def _build_object_pipeline() -> SensitiveObjectPipeline:
    protector = RegionProtector(blur_method=CONFIG.blur_method, blur_intensity=CONFIG.blur_intensity)
    if CONFIG.detector_provider == "yolo":
        detector = YOLODetector(model_path=CONFIG.yolo_model_path, device=CONFIG.detector_device)
        if not detector.available:
            log_event("detector_unavailable", provider="yolo", reason=detector.unavailable_reason)
            detector = UnavailableDetector(detector.unavailable_reason)
    else:
        detector = MockDetector()
    return SensitiveObjectPipeline(
        detector=detector,
        protector=protector,
        sensitive_labels=CONFIG.sensitive_labels,
        confidence_threshold=CONFIG.detection_confidence_threshold,
        inference_image_size=CONFIG.inference_image_size,
        detect_every_n_frames=CONFIG.detect_every_n_frames,
        bbox_smoothing_alpha=CONFIG.bbox_smoothing_alpha,
        detector_timeout_ms=CONFIG.detector_timeout_ms,
    )


def _build_face_pipeline(gallery: FaceGallery) -> FacePrivacyPipeline:
    protector = RegionProtector(blur_method=CONFIG.blur_method, blur_intensity=CONFIG.blur_intensity)
    base_dir = Path(__file__).resolve().parent
    model_dir = str((base_dir / CONFIG.arcface_model_dir).resolve())
    if CONFIG.face_provider == "insightface":
        recognizer = InsightFaceRecognizer(model_root=model_dir, device=CONFIG.detector_device)
        if not recognizer.available:
            log_event("face_recognizer_unavailable", provider="insightface", reason=recognizer.unavailable_reason)
            recognizer = MockFaceRecognizer()
    else:
        recognizer = MockFaceRecognizer()
    return FacePrivacyPipeline(
        recognizer=recognizer,
        protector=protector,
        gallery=gallery,
        match_threshold=CONFIG.face_match_threshold,
        detect_every_n_frames=CONFIG.face_detect_every_n_frames,
    )


METRICS = Metrics()
FACE_GALLERY = FaceGallery()
OBJECT_PIPELINE = _build_object_pipeline()
FACE_PIPELINE = _build_face_pipeline(FACE_GALLERY)
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
    detections_payload: list[dict[str, Any]] = []
    faces_payload: list[dict[str, Any]] = []
    detection_ms = 0.0
    face_ms = 0.0
    detect_count = 0
    blurred_count = 0
    faces_detected = 0
    faces_blurred = 0
    skipped_detection = False
    degraded = False

    if req.mode == "grayscale":
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        processed = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    else:
        processed = image
        run_object = req.enable_object_detection
        if run_object is None:
            run_object = bool(CONFIG.sensitive_labels)
        room_id = (req.room_id or "default").strip() or "default"

        if run_object:
            try:
                pipeline_out = OBJECT_PIPELINE.process(image)
                processed = pipeline_out["processed"]
                detection_ms = float(pipeline_out["detection_ms"])
                detect_count = int(pipeline_out["detect_count"])
                blurred_count = int(pipeline_out["blurred_count"])
                skipped_detection = bool(pipeline_out["skipped_detection"])
                if pipeline_out.get("degraded"):
                    degraded = True
                    log_event(
                        "process_degraded",
                        request_id=request_id,
                        trace_id=trace_id,
                        reason=pipeline_out.get("degraded_reason", "object_detector_failed"),
                    )
                detections_payload = [
                    {
                        "label": det.label,
                        "confidence": det.confidence,
                        "bbox": list(det.bbox),
                        "sensitive": det.sensitive,
                        "blurred": det.blurred,
                        "reused": det.reused,
                    }
                    for det in pipeline_out["detections"]
                ]
            except Exception as exc:  # pragma: no cover
                degraded = True
                log_event(
                    "process_degraded",
                    request_id=request_id,
                    trace_id=trace_id,
                    reason=f"object_detector_exception:{exc}",
                )

        if req.enable_face_privacy:
            try:
                face_out = FACE_PIPELINE.process(
                    processed,
                    room_id=room_id,
                    whitelist_user_ids=req.whitelist_user_ids,
                    enabled=True,
                )
                processed = face_out["processed"]
                face_ms = float(face_out["face_ms"])
                faces_detected = int(face_out["faces_detected"])
                faces_blurred = int(face_out["faces_blurred"])
                if face_out.get("degraded"):
                    degraded = True
                    log_event(
                        "process_degraded",
                        request_id=request_id,
                        trace_id=trace_id,
                        reason=face_out.get("degraded_reason", "face_pipeline_failed"),
                    )
                faces_payload = [
                    {
                        "bbox": list(f.bbox),
                        "confidence": f.confidence,
                        "matched_user": f.matched_user,
                        "similarity": f.similarity,
                        "blurred": f.blurred,
                    }
                    for f in face_out["faces"]
                ]
            except Exception as exc:  # pragma: no cover
                degraded = True
                log_event(
                    "process_degraded",
                    request_id=request_id,
                    trace_id=trace_id,
                    reason=f"face_pipeline_exception:{exc}",
                )

    inference_ms = (time.perf_counter() - infer_start) * 1000.0
    total_blurred = blurred_count + faces_blurred

    out = ProcessFrameResponse(
        request_id=request_id,
        trace_id=trace_id,
        model_version=model_version,
        policy_version=policy_version,
        image=encode_base64_image(processed, request_id, trace_id),
        latency_ms=(time.perf_counter() - start) * 1000.0,
        detections=detections_payload,
        blurred_count=total_blurred,
        faces=faces_payload,
        faces_detected=faces_detected,
        faces_blurred=faces_blurred,
    )
    METRICS.record(
        ok=True,
        latency_ms=out.latency_ms,
        inference_ms=inference_ms,
        detection_ms=detection_ms,
        detect_count=detect_count,
        blurred_count=total_blurred,
        skipped_detection=skipped_detection,
        degraded=degraded,
        faces_detected=faces_detected,
        faces_blurred=faces_blurred,
    )
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
        detection_ms=detection_ms,
        face_ms=face_ms,
        detected_count=detect_count,
        blurred_count=total_blurred,
        faces_detected=faces_detected,
        faces_blurred=faces_blurred,
        skipped_detection=skipped_detection,
        degraded=degraded,
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


@app.post(f"{CONFIG.api_prefix}/face/enroll")
def face_enroll(req: FaceEnrollRequest) -> dict:
    request_id = req.request_id or str(uuid.uuid4())
    trace_id = req.trace_id or str(uuid.uuid4())
    room_id = req.room_id.strip()
    user_id = req.user_id.strip()
    if not room_id or not user_id:
        raise ApiError("INVALID_ENROLL_PAYLOAD", "room_id and user_id are required", 400, request_id, trace_id)

    image = decode_base64_image(req.image, request_id, trace_id)
    faces = FACE_PIPELINE.recognizer.detect_faces(image)
    if not faces:
        raise ApiError("FACE_NOT_FOUND", "No face detected for enrollment", 400, request_id, trace_id)
    primary = max(faces, key=lambda f: (f.bbox[2] - f.bbox[0]) * (f.bbox[3] - f.bbox[1]))
    embedding = FACE_PIPELINE.recognizer.embed_face(image, primary.bbox)
    count = FACE_GALLERY.enroll(room_id, user_id, embedding)
    log_event(
        "face_enrolled",
        request_id=request_id,
        trace_id=trace_id,
        room_id=room_id,
        user_id=user_id,
        template_count=count,
    )
    return {
        "status": "enrolled",
        "room_id": room_id,
        "user_id": user_id,
        "template_count": count,
        "request_id": request_id,
        "trace_id": trace_id,
    }


@app.post(f"{CONFIG.api_prefix}/face/clear")
def face_clear(req: FaceClearRequest) -> dict:
    request_id = req.request_id or str(uuid.uuid4())
    trace_id = req.trace_id or str(uuid.uuid4())
    room_id = req.room_id.strip()
    if not room_id:
        raise ApiError("INVALID_CLEAR_PAYLOAD", "room_id is required", 400, request_id, trace_id)
    FACE_GALLERY.clear_room(room_id)
    log_event("face_gallery_cleared", request_id=request_id, trace_id=trace_id, room_id=room_id)
    return {"status": "cleared", "room_id": room_id, "request_id": request_id, "trace_id": trace_id}


@app.post("/process_frame")
def process_frame_legacy(req: ProcessFrameRequest) -> dict:
    """
    Backward-compatible endpoint for old demo clients.
    Legacy shape remains {"image": "..."} while internally using v1 contract.
    """
    out = process_frame_v1(req)
    return {"image": out.image}
