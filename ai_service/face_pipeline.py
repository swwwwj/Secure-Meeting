"""
Phase D: face detect -> embed -> match whitelist -> blur non-participants.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Literal

import cv2
import numpy as np

from pipeline import RegionProtector


@dataclass
class FaceBox:
    bbox: tuple[int, int, int, int]
    confidence: float = 1.0
    matched_user: str | None = None
    similarity: float = 0.0
    blurred: bool = False


class FaceRecognizer:
    def detect_faces(self, image: np.ndarray) -> list[FaceBox]:
        raise NotImplementedError

    def embed_face(self, image: np.ndarray, bbox: tuple[int, int, int, int]) -> np.ndarray:
        raise NotImplementedError

    @property
    def available(self) -> bool:
        return True


class MockFaceRecognizer(FaceRecognizer):
    """Deterministic faces for CI: primary center face + optional secondary face."""

    def detect_faces(self, image: np.ndarray) -> list[FaceBox]:
        h, w = image.shape[:2]
        fw = max(16, int(w * 0.28))
        fh = max(16, int(h * 0.34))
        cx = w // 2
        cy = h // 2
        faces = [
            FaceBox(
                bbox=(max(0, cx - fw // 2), max(0, cy - fh // 2), min(w, cx + fw // 2), min(h, cy + fh // 2)),
                confidence=0.95,
            )
        ]
        if w > 80 and h > 80:
            sw = max(12, fw // 2)
            sh = max(12, fh // 2)
            faces.append(
                FaceBox(
                    bbox=(max(0, w // 4 - sw // 2), max(0, h // 3 - sh // 2), min(w, w // 4 + sw // 2), min(h, h // 3 + sh // 2)),
                    confidence=0.88,
                )
            )
        return faces

    def embed_face(self, image: np.ndarray, bbox: tuple[int, int, int, int]) -> np.ndarray:
        x1, y1, x2, y2 = bbox
        roi = image[y1:y2, x1:x2]
        if roi.size == 0:
            return np.zeros(128, dtype=np.float32)
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        small = cv2.resize(gray, (32, 32), interpolation=cv2.INTER_AREA)
        vec = small.astype(np.float32).flatten()
        norm = np.linalg.norm(vec)
        if norm < 1e-6:
            return vec
        return vec / norm


class InsightFaceRecognizer(FaceRecognizer):
    def __init__(self, model_root: str, device: str = "cpu") -> None:
        self.model_root = model_root
        self.device = device
        self._app = None
        self._load_error: str | None = None

    def _ensure(self) -> None:
        if self._app is not None or self._load_error is not None:
            return
        try:
            from insightface.app import FaceAnalysis  # type: ignore
        except Exception as exc:  # pragma: no cover
            self._load_error = f"insightface import failed: {exc}"
            return
        try:
            ctx_id = -1 if self.device == "cpu" else 0
            self._app = FaceAnalysis(name="buffalo_l", root=self.model_root)
            self._app.prepare(ctx_id=ctx_id, det_size=(640, 640))
        except Exception as exc:  # pragma: no cover
            self._load_error = f"insightface init failed: {exc}"

    @property
    def available(self) -> bool:
        self._ensure()
        return self._app is not None

    @property
    def unavailable_reason(self) -> str:
        self._ensure()
        return self._load_error or "unknown"

    def detect_faces(self, image: np.ndarray) -> list[FaceBox]:
        self._ensure()
        if self._app is None:
            raise RuntimeError(self.unavailable_reason)
        faces: list[FaceBox] = []
        for face in self._app.get(image):
            x1, y1, x2, y2 = [int(v) for v in face.bbox.astype(int).tolist()]
            faces.append(FaceBox(bbox=(x1, y1, x2, y2), confidence=float(face.det_score)))
        return faces

    def embed_face(self, image: np.ndarray, bbox: tuple[int, int, int, int]) -> np.ndarray:
        self._ensure()
        if self._app is None:
            raise RuntimeError(self.unavailable_reason)
        faces = self._app.get(image)
        if not faces:
            return MockFaceRecognizer().embed_face(image, bbox)
        best = max(faces, key=lambda f: f.det_score)
        emb = np.asarray(best.normed_embedding, dtype=np.float32)
        if emb.size == 0:
            emb = np.asarray(best.embedding, dtype=np.float32)
        norm = np.linalg.norm(emb)
        if norm > 1e-6:
            emb = emb / norm
        return emb


class FaceGallery:
    """In-memory room -> user_id -> embedding templates."""

    def __init__(self) -> None:
        self._rooms: dict[str, dict[str, list[np.ndarray]]] = {}

    def enroll(self, room_id: str, user_id: str, embedding: np.ndarray) -> int:
        room = self._rooms.setdefault(room_id, {})
        templates = room.setdefault(user_id, [])
        templates.append(embedding)
        if len(templates) > 5:
            del templates[0]
        return len(templates)

    def clear_room(self, room_id: str) -> None:
        self._rooms.pop(room_id, None)

    def enrolled_users(self, room_id: str) -> list[str]:
        return list(self._rooms.get(room_id, {}).keys())

    def match_user(self, room_id: str, embedding: np.ndarray, whitelist: set[str], threshold: float) -> tuple[str | None, float]:
        room = self._rooms.get(room_id, {})
        best_user: str | None = None
        best_score = -1.0
        for user_id, templates in room.items():
            if whitelist and user_id not in whitelist:
                continue
            for template in templates:
                score = float(np.dot(embedding, template))
                if score > best_score:
                    best_score = score
                    best_user = user_id
        if best_user is not None and best_score >= threshold:
            return best_user, best_score
        return None, best_score


def cosine_similarity(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.dot(a, b))


class FacePrivacyPipeline:
    def __init__(
        self,
        recognizer: FaceRecognizer,
        protector: RegionProtector,
        gallery: FaceGallery,
        match_threshold: float,
        detect_every_n_frames: int = 1,
    ) -> None:
        self.recognizer = recognizer
        self.protector = protector
        self.gallery = gallery
        self.match_threshold = match_threshold
        self.detect_every_n_frames = max(1, detect_every_n_frames)
        self._frame_index = 0
        self._last_faces: list[FaceBox] = []

    def process(
        self,
        image: np.ndarray,
        room_id: str,
        whitelist_user_ids: list[str],
        enabled: bool,
    ) -> dict[str, Any]:
        if not enabled:
            return {
                "processed": image,
                "faces": [],
                "faces_detected": 0,
                "faces_blurred": 0,
                "face_ms": 0.0,
                "degraded": False,
            }

        whitelist = {u.strip() for u in whitelist_user_ids if u.strip()}
        start = time.perf_counter()
        try:
            self._frame_index += 1
            if self._frame_index % self.detect_every_n_frames == 0 or not self._last_faces:
                faces = self.recognizer.detect_faces(image)
                self._last_faces = faces
            else:
                faces = [FaceBox(bbox=f.bbox, confidence=f.confidence) for f in self._last_faces]

            to_blur: list[FaceBox] = []
            face_results: list[FaceBox] = []
            for face in faces:
                emb = self.recognizer.embed_face(image, face.bbox)
                matched_user, score = self.gallery.match_user(room_id, emb, whitelist, self.match_threshold)
                face.matched_user = matched_user
                face.similarity = score
                if matched_user is None:
                    to_blur.append(face)
                face_results.append(face)

            processed = image
            blurred_count = 0
            if to_blur:
                from pipeline import Detection

                det_list = [
                    Detection(label="face", confidence=f.confidence, bbox=f.bbox, sensitive=True)
                    for f in to_blur
                ]
                processed, blurred_count = self.protector.blur_regions(image, det_list)
                for f in to_blur:
                    f.blurred = True

            face_ms = (time.perf_counter() - start) * 1000.0
            return {
                "processed": processed,
                "faces": face_results,
                "faces_detected": len(face_results),
                "faces_blurred": blurred_count,
                "face_ms": face_ms,
                "degraded": False,
            }
        except Exception as exc:  # pragma: no cover
            return {
                "processed": image,
                "faces": [],
                "faces_detected": 0,
                "faces_blurred": 0,
                "face_ms": (time.perf_counter() - start) * 1000.0,
                "degraded": True,
                "degraded_reason": str(exc),
            }
