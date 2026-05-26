import base64
import importlib
import os

import cv2
import numpy as np
from fastapi.testclient import TestClient


def _make_image_b64() -> str:
    img = np.zeros((48, 48, 3), dtype=np.uint8)
    for y in range(img.shape[0]):
        for x in range(img.shape[1]):
            img[y, x, 0] = (x * 5 + y * 2) % 255
            img[y, x, 1] = (x * 3 + 40) % 255
            img[y, x, 2] = (y * 4 + 80) % 255
    ok, encoded = cv2.imencode(".png", img)
    assert ok
    return base64.b64encode(encoded.tobytes()).decode("utf-8")


def _decode_image(image_b64: str) -> np.ndarray:
    raw = base64.b64decode(image_b64)
    np_buffer = np.frombuffer(raw, dtype=np.uint8)
    image = cv2.imdecode(np_buffer, cv2.IMREAD_COLOR)
    assert image is not None
    return image


def _client() -> TestClient:
    os.environ.pop("SM_ENV", None)
    os.environ.pop("SM_MODEL_VERSION", None)
    os.environ.pop("SM_POLICY_VERSION", None)
    os.environ.pop("SM_API_PREFIX", None)
    os.environ.pop("SM_DETECTOR_PROVIDER", None)
    os.environ.pop("SM_DETECTOR_DEVICE", None)
    os.environ.pop("SM_YOLO_MODEL_PATH", None)
    os.environ.pop("SM_SENSITIVE_LABELS", None)
    os.environ["SM_ENV"] = "test"
    mod = importlib.import_module("app")
    importlib.reload(mod)
    return TestClient(mod.app)


def _client_with_overrides(**env_vars: str) -> TestClient:
    _client()
    os.environ["SM_ENV"] = "test"
    for key, value in env_vars.items():
        os.environ[key] = value
    mod = importlib.import_module("app")
    importlib.reload(mod)
    return TestClient(mod.app)


def test_process_frame_v1_success():
    client = _client()
    payload = {"image": _make_image_b64(), "mode": "pass", "request_id": "req-1", "trace_id": "trace-1"}
    resp = client.post("/api/v1/process_frame", json=payload)
    assert resp.status_code == 200
    body = resp.json()
    assert body["request_id"] == "req-1"
    assert body["trace_id"] == "trace-1"
    assert body["model_version"]
    assert body["policy_version"]
    assert body["latency_ms"] >= 0
    assert body["blurred_count"] >= 1
    assert isinstance(body["detections"], list)
    out = _decode_image(body["image"])
    # Sensitive "id_card" region should be blurred under mock detector path.
    assert not np.array_equal(out, _decode_image(payload["image"]))


def test_process_frame_v1_invalid_payload():
    client = _client()
    resp = client.post("/api/v1/process_frame", json={"image": "not-base64"})
    assert resp.status_code == 400
    body = resp.json()
    assert body["error"]["code"] in {"INVALID_IMAGE_PAYLOAD", "IMAGE_DECODE_FAILED"}
    assert body["error"]["request_id"]
    assert body["error"]["trace_id"]


def test_process_frame_v1_forced_error():
    client = _client()
    payload = {"image": _make_image_b64(), "debug": {"force_error": True}}
    resp = client.post("/api/v1/process_frame", json=payload)
    assert resp.status_code == 500
    assert resp.json()["error"]["code"] == "DEBUG_FORCED_ERROR"


def test_process_frame_v1_delay_metrics_and_legacy_compat():
    client = _client()
    payload = {"image": _make_image_b64(), "debug": {"simulate_delay_ms": 20}}
    v1_resp = client.post("/api/v1/process_frame", json=payload)
    assert v1_resp.status_code == 200
    assert v1_resp.json()["latency_ms"] >= 20

    legacy_resp = client.post("/process_frame", json={"image": _make_image_b64()})
    assert legacy_resp.status_code == 200
    assert "image" in legacy_resp.json()
    assert "request_id" not in legacy_resp.json()

    metrics_resp = client.get("/api/v1/metrics")
    assert metrics_resp.status_code == 200
    metrics = metrics_resp.json()
    assert metrics["requests_total"] >= 2
    assert metrics["throughput_fps"] > 0


def test_config_override_from_env():
    client = _client_with_overrides(
        SM_MODEL_VERSION="override-model",
        SM_POLICY_VERSION="override-policy",
        SM_API_PREFIX="/api/v99",
    )
    payload = {"image": _make_image_b64()}
    resp = client.post("/api/v99/process_frame", json=payload)
    assert resp.status_code == 200
    body = resp.json()
    assert body["model_version"] == "override-model"
    assert body["policy_version"] == "override-policy"


def test_sensitive_list_miss_keeps_image_clear():
    client = _client_with_overrides(SM_SENSITIVE_LABELS='["cell phone"]')
    source = _make_image_b64()
    resp = client.post("/api/v1/process_frame", json={"image": source, "mode": "pass"})
    assert resp.status_code == 200
    body = resp.json()
    assert body["blurred_count"] == 0
    out = _decode_image(body["image"])
    original = _decode_image(source)
    assert np.array_equal(out, original)


def test_sensitive_list_empty_disables_blur():
    client = _client_with_overrides(SM_SENSITIVE_LABELS="[]")
    source = _make_image_b64()
    resp = client.post("/api/v1/process_frame", json={"image": source, "mode": "pass"})
    assert resp.status_code == 200
    body = resp.json()
    assert body["blurred_count"] == 0
    assert body["detections"] == []
    out = _decode_image(body["image"])
    assert np.array_equal(out, _decode_image(source))


def test_detector_failure_degrades_to_passthrough():
    client = _client_with_overrides(SM_DETECTOR_PROVIDER="yolo", SM_YOLO_MODEL_PATH="./non-existing-model.pt")
    source = _make_image_b64()
    resp = client.post("/api/v1/process_frame", json={"image": source, "mode": "pass"})
    assert resp.status_code == 200
    body = resp.json()
    assert body["blurred_count"] == 0
    out = _decode_image(body["image"])
    assert np.array_equal(out, _decode_image(source))
