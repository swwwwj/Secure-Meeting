"""
Phase E: adversarial perturbation protector (pluggable, mock or learned weights).
"""

from __future__ import annotations

from pathlib import Path
from typing import Literal

import cv2
import numpy as np

from perturbation_fast import apply_pixel_deltas, is_pixel_delta_checkpoint, load_checkpoint
from pipeline import Detection, RegionProtector


class PerturbationProtector:
    """Apply bounded adversarial-style noise inside detection ROIs."""

    def __init__(self, strength: float = 0.08, provider: Literal["mock", "learned"] = "mock", weights_path: str = "") -> None:
        self.strength = max(0.01, min(float(strength), 0.35))
        self.provider = provider
        self.weights_path = weights_path
        self._generator = None
        self._pixel_delta: dict | None = None
        self._load_error: str | None = None
        self.model_version = "adv-perturb-mock-v1"
        if provider == "learned":
            self._load_generator()

    @property
    def available(self) -> bool:
        if self.provider == "mock":
            return True
        return self._generator is not None or self._pixel_delta is not None

    @property
    def unavailable_reason(self) -> str:
        return self._load_error or "unknown"

    def _load_generator(self) -> None:
        path = Path(self.weights_path)
        if not path.is_file():
            self._load_error = f"weights not found: {path}"
            return
        try:
            ckpt = load_checkpoint(path)
            if is_pixel_delta_checkpoint(ckpt):
                self._pixel_delta = ckpt
                self.strength = float(ckpt.get("epsilon", self.strength))
                self.model_version = str(ckpt.get("model_version", "adv-perturb-pixel-delta-v1"))
                return
            self._load_legacy_torch_weights(ckpt)
        except Exception as exc:  # pragma: no cover
            self._load_error = f"weights load failed: {exc}"

    def _load_legacy_torch_weights(self, ckpt: dict) -> None:
        try:
            import torch
            from perturbation_model import PerturbationGenerator
        except Exception as exc:  # pragma: no cover
            self._load_error = f"legacy torch weights require torch: {exc}"
            return
        epsilon = float(ckpt.get("epsilon", self.strength))
        model = PerturbationGenerator(epsilon=epsilon)
        state = ckpt.get("model_state", ckpt)
        model.load_state_dict(state)
        model.eval()
        self._generator = model
        self.model_version = str(ckpt.get("model_version", "adv-perturb-learned-v1"))

    def blur_regions(self, image: np.ndarray, detections: list[Detection]) -> tuple[np.ndarray, int]:
        """Same contract as RegionProtector.blur_regions for pipeline compatibility."""
        output = image.copy()
        applied = 0
        for det in detections:
            if not det.sensitive:
                continue
            x1, y1, x2, y2 = det.bbox
            h, w = output.shape[:2]
            x1 = max(0, min(x1, w - 1))
            y1 = max(0, min(y1, h - 1))
            x2 = max(0, min(x2, w))
            y2 = max(0, min(y2, h))
            if x2 <= x1 or y2 <= y1:
                continue
            roi = output[y1:y2, x1:x2]
            if roi.size == 0:
                continue
            output[y1:y2, x1:x2] = self._perturb_roi(roi, det.bbox)
            det.blurred = True
            applied += 1
        return output, applied

    def _perturb_roi(self, roi: np.ndarray, bbox: tuple[int, int, int, int]) -> np.ndarray:
        if self.provider == "learned" and self._pixel_delta is not None:
            pixels = self._pixel_delta.get("pixels", [])
            return apply_pixel_deltas(roi, pixels, bbox=bbox)
        if self.provider == "learned" and self._generator is not None:
            return self._perturb_learned(roi)
        return self._perturb_mock(roi, bbox)

    def _perturb_mock(self, roi: np.ndarray, bbox: tuple[int, int, int, int]) -> np.ndarray:
        x1, y1, x2, y2 = bbox
        h, w = roi.shape[:2]
        yy, xx = np.mgrid[0:h, 0:w]
        seed = (x1 * 17 + y1 * 31 + x2 * 13 + y2 * 7) % 997
        phase = seed * 0.17
        pattern = np.sin(xx * 0.65 + phase) * np.cos(yy * 0.55 - phase)
        noise = (pattern * 127.0 * self.strength).astype(np.float32)
        out = roi.astype(np.float32) + noise[:, :, None]
        return np.clip(out, 0, 255).astype(np.uint8)

    def _perturb_learned(self, roi: np.ndarray) -> np.ndarray:
        import torch

        tensor = torch.from_numpy(roi).float().permute(2, 0, 1).unsqueeze(0) / 255.0
        with torch.no_grad():
            protected = self._generator(tensor)
        out = protected.squeeze(0).permute(1, 2, 0).numpy()
        return np.clip(out * 255.0, 0, 255).astype(np.uint8)


class HybridFaceProtector:
    """Perturbation first, then light blur for hybrid privacy mode."""

    def __init__(
        self,
        perturbation: PerturbationProtector,
        blur_method: Literal["gaussian", "mosaic"] = "gaussian",
        blur_intensity: int = 9,
    ) -> None:
        self.perturbation = perturbation
        self.blur = RegionProtector(blur_method=blur_method, blur_intensity=blur_intensity)

    def blur_regions(self, image: np.ndarray, detections: list[Detection]) -> tuple[np.ndarray, int]:
        perturbed, count = self.perturbation.blur_regions(image, detections)
        blurred, blur_count = self.blur.blur_regions(perturbed, detections)
        return blurred, max(count, blur_count)


def build_face_protector(
    mode: Literal["none", "blur", "perturbation", "hybrid"],
    blur_method: Literal["gaussian", "mosaic"],
    blur_intensity: int,
    perturbation_provider: Literal["mock", "learned"],
    perturbation_weights_path: str,
    perturbation_epsilon: float,
) -> RegionProtector | PerturbationProtector | HybridFaceProtector | None:
    if mode == "none":
        return None
    if mode == "blur":
        return RegionProtector(blur_method=blur_method, blur_intensity=blur_intensity)
    perturb = PerturbationProtector(
        strength=perturbation_epsilon,
        provider=perturbation_provider,
        weights_path=perturbation_weights_path,
    )
    if mode == "perturbation":
        if perturbation_provider == "learned" and not perturb.available:
            return perturb  # pipeline will degrade; mock fallback handled in app
        return perturb
    return HybridFaceProtector(
        perturbation=perturb,
        blur_method=blur_method,
        blur_intensity=max(5, blur_intensity // 2),
    )
