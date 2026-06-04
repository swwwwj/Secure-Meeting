"""
Fast CPU-only adversarial perturbation: fixed sparse pixel deltas (no GPU / no real training).

Checkpoint type: pixel_delta_v1
"""

from __future__ import annotations

import pickle
import random
from pathlib import Path
from typing import Any

import numpy as np

CHECKPOINT_TYPE = "pixel_delta_v1"
DEFAULT_MODEL_VERSION = "adv-perturb-pixel-delta-v1"


def build_pixel_deltas(
    seed: int = 42,
    num_pixels: int = 12,
    max_channel_delta: int = 3,
) -> list[dict[str, int | float]]:
    """Deterministic list of relative pixel edits (fractional position + BGR delta)."""
    rng = random.Random(seed)
    pixels: list[dict[str, int | float]] = []
    for _ in range(num_pixels):
        pixels.append(
            {
                "y": round(rng.random(), 6),
                "x": round(rng.random(), 6),
                "db": rng.randint(-max_channel_delta, max_channel_delta),
                "dg": rng.randint(-max_channel_delta, max_channel_delta),
                "dr": rng.randint(-max_channel_delta, max_channel_delta),
            }
        )
    return pixels


def bbox_seed(bbox: tuple[int, int, int, int]) -> int:
    x1, y1, x2, y2 = bbox
    return (x1 * 17 + y1 * 31 + x2 * 13 + y2 * 7) % 1_000_000


def apply_pixel_deltas(
    roi: np.ndarray,
    pixels: list[dict[str, int | float]],
    bbox: tuple[int, int, int, int] | None = None,
) -> np.ndarray:
    """Apply a few pixel edits; image stays visually almost identical."""
    if roi.size == 0 or not pixels:
        return roi
    out = roi.copy()
    h, w = out.shape[:2]
    shift = bbox_seed(bbox) if bbox else 0
    for index, pixel in enumerate(pixels):
        py = int(float(pixel["y"]) * (h - 1))
        px = int(float(pixel["x"]) * (w - 1))
        if bbox is not None:
            py = (py + (shift + index * 3) % max(h, 1)) % h
            px = (px + (shift + index * 5) % max(w, 1)) % w
        py = max(0, min(py, h - 1))
        px = max(0, min(px, w - 1))
        b, g, r = out[py, px].astype(np.int16)
        out[py, px, 0] = np.clip(b + int(pixel["db"]), 0, 255)
        out[py, px, 1] = np.clip(g + int(pixel["dg"]), 0, 255)
        out[py, px, 2] = np.clip(r + int(pixel["dr"]), 0, 255)
    return out


def build_checkpoint(
    seed: int = 42,
    num_pixels: int = 12,
    max_channel_delta: int = 3,
    epsilon: float = 0.08,
    samples_seen: int = 0,
) -> dict[str, Any]:
    return {
        "checkpoint_type": CHECKPOINT_TYPE,
        "model_version": DEFAULT_MODEL_VERSION,
        "epsilon": float(epsilon),
        "num_pixels": num_pixels,
        "max_channel_delta": max_channel_delta,
        "samples_seen": samples_seen,
        "pixels": build_pixel_deltas(seed=seed, num_pixels=num_pixels, max_channel_delta=max_channel_delta),
    }


def save_checkpoint(ckpt: dict[str, Any], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        import torch

        torch.save(ckpt, path)
    except Exception:
        with path.open("wb") as handle:
            pickle.dump(ckpt, handle)


def load_checkpoint(path: Path) -> dict[str, Any]:
    try:
        import torch

        obj = torch.load(path, map_location="cpu", weights_only=False)
    except TypeError:
        import torch

        obj = torch.load(path, map_location="cpu")
    except Exception:
        with path.open("rb") as handle:
            obj = pickle.load(handle)
    if not isinstance(obj, dict):
        raise RuntimeError(f"invalid checkpoint format: {path}")
    return obj


def is_pixel_delta_checkpoint(ckpt: dict[str, Any]) -> bool:
    return ckpt.get("checkpoint_type") == CHECKPOINT_TYPE
