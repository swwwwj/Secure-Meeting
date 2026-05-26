import time
from dataclasses import dataclass
from typing import Any, Literal

import cv2
import numpy as np


@dataclass
class Detection:
    label: str
    confidence: float
    bbox: tuple[int, int, int, int]
    sensitive: bool = False
    blurred: bool = False
    reused: bool = False


class Detector:
    def detect(self, image: np.ndarray, confidence_threshold: float, image_size: int) -> list[Detection]:
        raise NotImplementedError


class MockDetector(Detector):
    """
    Deterministic detector for tests and local fallback:
    returns one center box labeled "id_card".
    """

    def detect(self, image: np.ndarray, confidence_threshold: float, image_size: int) -> list[Detection]:
        h, w = image.shape[:2]
        box_w = max(12, int(w * 0.35))
        box_h = max(12, int(h * 0.35))
        x1 = max(0, (w - box_w) // 2)
        y1 = max(0, (h - box_h) // 2)
        x2 = min(w, x1 + box_w)
        y2 = min(h, y1 + box_h)
        return [Detection(label="id_card", confidence=max(confidence_threshold, 0.9), bbox=(x1, y1, x2, y2))]


class UnavailableDetector(Detector):
    def __init__(self, reason: str) -> None:
        self.reason = reason

    def detect(self, image: np.ndarray, confidence_threshold: float, image_size: int) -> list[Detection]:
        raise RuntimeError(self.reason)


class YOLODetector(Detector):
    def __init__(self, model_path: str, device: str = "cpu") -> None:
        self.model_path = model_path
        self.device = device
        self._model = None
        self._load_error: str | None = None

    def _ensure_model(self) -> None:
        if self._model is not None or self._load_error is not None:
            return
        try:
            from ultralytics import YOLO  # type: ignore
        except Exception as exc:  # pragma: no cover - dependency optional in test CI
            self._load_error = f"ultralytics import failed: {exc}"
            return
        try:
            self._model = YOLO(self.model_path)
        except Exception as exc:  # pragma: no cover - model errors env-dependent
            self._load_error = f"model load failed: {exc}"

    @property
    def available(self) -> bool:
        self._ensure_model()
        return self._model is not None

    @property
    def unavailable_reason(self) -> str:
        self._ensure_model()
        return self._load_error or "unknown"

    def detect(self, image: np.ndarray, confidence_threshold: float, image_size: int) -> list[Detection]:
        self._ensure_model()
        if self._model is None:
            raise RuntimeError(self.unavailable_reason)
        results = self._model.predict(image, conf=confidence_threshold, imgsz=image_size, device=self.device, verbose=False)
        detections: list[Detection] = []
        for result in results:
            names = result.names
            if result.boxes is None:
                continue
            for box in result.boxes:
                cls_id = int(box.cls[0].item())
                label = str(names.get(cls_id, str(cls_id)))
                conf = float(box.conf[0].item())
                x1, y1, x2, y2 = [int(v) for v in box.xyxy[0].tolist()]
                detections.append(Detection(label=label, confidence=conf, bbox=(x1, y1, x2, y2)))
        return detections


class RegionProtector:
    def __init__(self, blur_method: Literal["gaussian", "mosaic"], blur_intensity: int) -> None:
        self.blur_method = blur_method
        self.blur_intensity = max(3, blur_intensity)

    def blur_regions(self, image: np.ndarray, detections: list[Detection]) -> tuple[np.ndarray, int]:
        output = image.copy()
        h, w = output.shape[:2]
        blurred = 0
        for det in detections:
            if not det.sensitive:
                continue
            x1, y1, x2, y2 = det.bbox
            x1 = max(0, min(x1, w - 1))
            y1 = max(0, min(y1, h - 1))
            x2 = max(0, min(x2, w))
            y2 = max(0, min(y2, h))
            if x2 <= x1 or y2 <= y1:
                continue
            roi = output[y1:y2, x1:x2]
            if roi.size == 0:
                continue
            if self.blur_method == "mosaic":
                output[y1:y2, x1:x2] = self._mosaic(roi)
            else:
                output[y1:y2, x1:x2] = self._gaussian(roi)
            det.blurred = True
            blurred += 1
        return output, blurred

    def _gaussian(self, roi: np.ndarray) -> np.ndarray:
        k = self.blur_intensity
        if k % 2 == 0:
            k += 1
        return cv2.GaussianBlur(roi, (k, k), sigmaX=0)

    def _mosaic(self, roi: np.ndarray) -> np.ndarray:
        h, w = roi.shape[:2]
        down_w = max(1, w // max(2, self.blur_intensity))
        down_h = max(1, h // max(2, self.blur_intensity))
        tiny = cv2.resize(roi, (down_w, down_h), interpolation=cv2.INTER_LINEAR)
        return cv2.resize(tiny, (w, h), interpolation=cv2.INTER_NEAREST)


def _iou(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> float:
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    ix1 = max(ax1, bx1)
    iy1 = max(ay1, by1)
    ix2 = min(ax2, bx2)
    iy2 = min(ay2, by2)
    iw = max(0, ix2 - ix1)
    ih = max(0, iy2 - iy1)
    inter = iw * ih
    if inter == 0:
        return 0.0
    area_a = max(0, ax2 - ax1) * max(0, ay2 - ay1)
    area_b = max(0, bx2 - bx1) * max(0, by2 - by1)
    union = area_a + area_b - inter
    return inter / union if union > 0 else 0.0


def smooth_boxes(current: list[Detection], previous: list[Detection], alpha: float) -> list[Detection]:
    if not previous:
        return current
    out: list[Detection] = []
    for cur in current:
        best_prev: Detection | None = None
        best_iou = 0.0
        for prev in previous:
            if prev.label != cur.label:
                continue
            overlap = _iou(cur.bbox, prev.bbox)
            if overlap > best_iou:
                best_iou = overlap
                best_prev = prev
        if best_prev is not None and best_iou > 0.1:
            cx1, cy1, cx2, cy2 = cur.bbox
            px1, py1, px2, py2 = best_prev.bbox
            cur.bbox = (
                int(alpha * cx1 + (1.0 - alpha) * px1),
                int(alpha * cy1 + (1.0 - alpha) * py1),
                int(alpha * cx2 + (1.0 - alpha) * px2),
                int(alpha * cy2 + (1.0 - alpha) * py2),
            )
        out.append(cur)
    return out


class SensitiveObjectPipeline:
    def __init__(
        self,
        detector: Detector,
        protector: RegionProtector,
        sensitive_labels: list[str],
        confidence_threshold: float,
        inference_image_size: int,
        detect_every_n_frames: int,
        bbox_smoothing_alpha: float,
        detector_timeout_ms: int,
    ) -> None:
        self.detector = detector
        self.protector = protector
        self.sensitive_labels = {label.strip().lower() for label in sensitive_labels if label.strip()}
        self.confidence_threshold = confidence_threshold
        self.inference_image_size = inference_image_size
        self.detect_every_n_frames = max(1, detect_every_n_frames)
        self.bbox_smoothing_alpha = max(0.0, min(1.0, bbox_smoothing_alpha))
        self.detector_timeout_ms = max(1, detector_timeout_ms)
        self._frame_index = 0
        self._last_detections: list[Detection] = []

    def process(self, image: np.ndarray) -> dict[str, Any]:
        self._frame_index += 1
        if not self.sensitive_labels:
            return {
                "processed": image,
                "detections": [],
                "blurred_count": 0,
                "detect_count": 0,
                "detection_ms": 0.0,
                "skipped_detection": True,
                "degraded": False,
            }

        skipped = (self._frame_index % self.detect_every_n_frames) != 0
        detect_start = time.perf_counter()
        detections: list[Detection]
        if skipped and self._last_detections:
            detections = [
                Detection(
                    label=det.label,
                    confidence=det.confidence,
                    bbox=det.bbox,
                    sensitive=det.sensitive,
                    reused=True,
                )
                for det in self._last_detections
            ]
            detection_ms = 0.0
        else:
            detections = self.detector.detect(image, self.confidence_threshold, self.inference_image_size)
            detection_ms = (time.perf_counter() - detect_start) * 1000.0
            if detection_ms > self.detector_timeout_ms:
                return {
                    "processed": image,
                    "detections": [],
                    "blurred_count": 0,
                    "detect_count": 0,
                    "detection_ms": detection_ms,
                    "skipped_detection": False,
                    "degraded": True,
                    "degraded_reason": "detector_timeout",
                }
            detections = smooth_boxes(detections, self._last_detections, self.bbox_smoothing_alpha)

        for det in detections:
            det.sensitive = det.label.lower() in self.sensitive_labels
        sensitive_detections = [det for det in detections if det.sensitive]
        processed, blurred_count = self.protector.blur_regions(image, sensitive_detections)
        self._last_detections = detections

        return {
            "processed": processed,
            "detections": detections,
            "blurred_count": blurred_count,
            "detect_count": len(detections),
            "detection_ms": detection_ms,
            "skipped_detection": skipped and bool(detections),
            "degraded": False,
        }
