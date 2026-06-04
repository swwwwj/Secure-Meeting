"""
Train adversarial perturbation generator weights (Phase E).

Run from ai_service directory:
  python -m training.train_adversarial_perturbation
  python -m training.train_adversarial_perturbation --data ./training/perturbation_samples --epochs 30

After training, set in config/dev.json:
  perturbation_provider: "learned"
  perturbation_weights_path: training/runs/adversarial_perturbation/weights/best.pt
  default_privacy_protect_mode: "perturbation"
"""

from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path

import cv2
import numpy as np

TRAINING_DIR = Path(__file__).resolve().parent
AI_SERVICE_DIR = TRAINING_DIR.parent
DEFAULT_PROJECT = TRAINING_DIR / "runs" / "adversarial_perturbation"
DEFAULT_WEIGHTS = DEFAULT_PROJECT / "weights" / "best.pt"


def _list_images(data_dir: Path) -> list[Path]:
    patterns = ("*.jpg", "*.jpeg", "*.png", "*.bmp", "*.webp")
    files: list[Path] = []
    for pattern in patterns:
        files.extend(data_dir.rglob(pattern))
    return sorted({p.resolve() for p in files})


def _load_patch(path: Path, patch_size: int) -> np.ndarray:
    image = cv2.imread(str(path))
    if image is None:
        raise RuntimeError(f"failed to read image: {path}")
    h, w = image.shape[:2]
    if h < patch_size or w < patch_size:
        image = cv2.resize(image, (patch_size, patch_size), interpolation=cv2.INTER_AREA)
        h, w = patch_size, patch_size
    y = random.randint(0, h - patch_size)
    x = random.randint(0, w - patch_size)
    patch = image[y : y + patch_size, x : x + patch_size]
    return patch.astype(np.float32) / 255.0


def _synthetic_batch(batch_size: int, patch_size: int) -> np.ndarray:
    batch = np.zeros((batch_size, patch_size, patch_size, 3), dtype=np.float32)
    for i in range(batch_size):
        base = np.random.rand(patch_size, patch_size, 3).astype(np.float32)
        yy, xx = np.mgrid[0:patch_size, 0:patch_size]
        wave = np.sin(xx * 0.2 + i) * np.cos(yy * 0.17 + i * 0.3)
        batch[i] = np.clip(base + wave[:, :, None] * 0.15, 0.0, 1.0)
    return batch


def main() -> int:
    parser = argparse.ArgumentParser(description="Train adversarial perturbation generator.")
    parser.add_argument("--data", default="", help="Optional directory of face/selfie images.")
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--patch-size", type=int, default=128)
    parser.add_argument("--epsilon", type=float, default=0.08)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--project", default=str(DEFAULT_PROJECT))
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    try:
        import torch
        import torch.nn.functional as F
    except Exception as exc:
        print("PyTorch is required for training. Install with: pip install torch")
        print(exc)
        return 1

    sys.path.insert(0, str(AI_SERVICE_DIR))
    from perturbation_model import PerturbationGenerator

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    data_dir = Path(args.data).resolve() if args.data else None
    image_files = _list_images(data_dir) if data_dir and data_dir.is_dir() else []
    if data_dir and not image_files:
        print(f"No images found under {data_dir}, using synthetic patches.")
        image_files = []

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = PerturbationGenerator(epsilon=args.epsilon).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)

    project = Path(args.project)
    weights_dir = project / "weights"
    weights_dir.mkdir(parents=True, exist_ok=True)
    best_path = weights_dir / "best.pt"

    best_loss = float("inf")
    for epoch in range(1, args.epochs + 1):
        model.train()
        epoch_loss = 0.0
        steps = max(20, len(image_files) // max(args.batch, 1))
        for _ in range(steps):
            if image_files:
                patches = [_load_patch(random.choice(image_files), args.patch_size) for _ in range(args.batch)]
                batch = torch.from_numpy(np.stack(patches, axis=0)).permute(0, 3, 1, 2).to(device)
            else:
                batch = torch.from_numpy(_synthetic_batch(args.batch, args.patch_size)).permute(0, 3, 1, 2).to(device)

            optimizer.zero_grad()
            protected = model(batch)
            delta = protected - batch
            l_inf = delta.abs().max()
            l2 = F.mse_loss(protected, batch)
            tv = (delta[:, :, 1:, :] - delta[:, :, :-1, :]).abs().mean() + (delta[:, :, :, 1:] - delta[:, :, :, :-1]).abs().mean()
            identity_break = -F.mse_loss(F.avg_pool2d(protected, 4), F.avg_pool2d(batch, 4))
            loss = l2 * 0.35 + tv * 0.15 + identity_break * 0.5
            if l_inf > args.epsilon:
                loss = loss + (l_inf - args.epsilon) * 10.0
            loss.backward()
            optimizer.step()
            epoch_loss += float(loss.item())

        avg_loss = epoch_loss / steps
        print(f"epoch={epoch}/{args.epochs} loss={avg_loss:.6f}")
        if avg_loss < best_loss:
            best_loss = avg_loss
            torch.save(
                {
                    "model_state": model.state_dict(),
                    "epsilon": args.epsilon,
                    "patch_size": args.patch_size,
                    "model_version": "adv-perturb-v1",
                },
                best_path,
            )

    rel = best_path
    try:
        rel = best_path.relative_to(AI_SERVICE_DIR)
    except ValueError:
        pass
    print("\n=== Training finished ===")
    print(f"Best weights: {best_path}")
    print("\nUpdate ai_service/config/dev.json:")
    print(
        json.dumps(
            {
                "perturbation_provider": "learned",
                "perturbation_weights_path": rel.as_posix(),
                "default_privacy_protect_mode": "perturbation",
            },
            indent=2,
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
