import base64
import importlib
import os

import cv2
import numpy as np
from fastapi.testclient import TestClient


def _make_image_b64() -> str:
    img = np.zeros((24, 24, 3), dtype=np.uint8)
    img[:, :, 0] = 20
    img[:, :, 1] = 140
    img[:, :, 2] = 220
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
    os.environ.pop("SM_MODEL_VERSION", None)
    os.environ.pop("SM_POLICY_VERSION", None)
    os.environ.pop("SM_API_PREFIX", None)
    os.environ["SM_ENV"] = "test"
    mod = importlib.import_module("app")
    importlib.reload(mod)
    return TestClient(mod.app)


def _client_with_overrides(**env_vars: str) -> TestClient:
    os.environ["SM_ENV"] = "test"
    for key, value in env_vars.items():
        os.environ[key] = value
    mod = importlib.import_module("app")
    importlib.reload(mod)
    return TestClient(mod.app)


def test_process_frame_v1_success():
    client = _client()
    payload = {"image": _make_image_b64(), "mode": "grayscale", "request_id": "req-1", "trace_id": "trace-1"}
    resp = client.post("/api/v1/process_frame", json=payload)
    assert resp.status_code == 200
    body = resp.json()
    assert body["request_id"] == "req-1"
    assert body["trace_id"] == "trace-1"
    assert body["model_version"]
    assert body["policy_version"]
    assert body["latency_ms"] >= 0
    out = _decode_image(body["image"])
    # Regression baseline: grayscale output should have equal channels.
    assert np.array_equal(out[:, :, 0], out[:, :, 1])
    assert np.array_equal(out[:, :, 1], out[:, :, 2])


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
