"""
Train adversarial perturbation weights (Phase E) — CPU-friendly lightweight export.

This script is intentionally light: no GPU, no PyTorch training loop.
It scans optional images for logging, runs a short fake epoch progress display,
then writes a checkpoint that only applies a few deterministic pixel edits at inference.

Run from ai_service directory:
  python -m training.train_adversarial_perturbation
  python -m training.train_adversarial_perturbation --data ./training/perturbation_samples --epochs 5

Output:
  training/runs/adversarial_perturbation/weights/best.pt  (pixel_delta_v1)
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

TRAINING_DIR = Path(__file__).resolve().parent
AI_SERVICE_DIR = TRAINING_DIR.parent
DEFAULT_PROJECT = TRAINING_DIR / "runs" / "adversarial_perturbation"
DEFAULT_WEIGHTS = DEFAULT_PROJECT / "weights" / "best.pt"

sys.path.insert(0, str(AI_SERVICE_DIR))
from perturbation_fast import build_checkpoint, save_checkpoint  # noqa: E402


def _list_images(data_dir: Path) -> list[Path]:
    patterns = ("*.jpg", "*.jpeg", "*.png", "*.bmp", "*.webp")
    files: list[Path] = []
    for pattern in patterns:
        files.extend(data_dir.rglob(pattern))
    return sorted({p.resolve() for p in files})


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export fast pixel-delta perturbation weights (CPU, no GPU required)."
    )
    parser.add_argument("--data", default="", help="Optional image folder (only used for sample count).")
    parser.add_argument("--epochs", type=int, default=3, help="Fake epoch progress iterations.")
    parser.add_argument("--num-pixels", type=int, default=12, help="Pixels to tweak per face region.")
    parser.add_argument("--max-delta", type=int, default=3, help="Per-channel delta magnitude (1-8).")
    parser.add_argument("--epsilon", type=float, default=0.08, help="Recorded metadata for config.")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--project", default=str(DEFAULT_PROJECT))
    args = parser.parse_args()

    data_dir = Path(args.data).resolve() if args.data else None
    image_files = _list_images(data_dir) if data_dir and data_dir.is_dir() else []
    samples_seen = len(image_files)

    print("=== Fast perturbation export (CPU) ===")
    print("No GPU or neural training; exporting sparse pixel-delta checkpoint.")
    if data_dir:
        print(f"Data dir: {data_dir} ({samples_seen} images scanned)")
    else:
        print("Data dir: (none) — using default seed only")

    for epoch in range(1, max(1, args.epochs) + 1):
        progress = epoch / max(1, args.epochs)
        print(f"epoch {epoch}/{args.epochs} [{progress * 100:.0f}%] preparing pixel pattern...")
        time.sleep(0.15)

    ckpt = build_checkpoint(
        seed=args.seed,
        num_pixels=max(4, min(args.num_pixels, 64)),
        max_channel_delta=max(1, min(args.max_delta, 8)),
        epsilon=args.epsilon,
        samples_seen=samples_seen,
    )

    weights_dir = Path(args.project) / "weights"
    best_path = weights_dir / "best.pt"
    save_checkpoint(ckpt, best_path)

    rel = best_path
    try:
        rel = best_path.relative_to(AI_SERVICE_DIR)
    except ValueError:
        pass

    print("\n=== Export finished ===")
    print(f"Weights: {best_path}")
    print(f"Type: {ckpt['checkpoint_type']} | pixels/roi: {ckpt['num_pixels']} | max delta: {ckpt['max_channel_delta']}")
    print("\nUpdate ai_service/config/dev.json:")
    print(
        json.dumps(
            {
                "perturbation_provider": "learned",
                "perturbation_weights_path": rel.as_posix(),
                "default_privacy_protect_mode": "perturbation",
                "perturbation_model_version": ckpt["model_version"],
            },
            indent=2,
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
