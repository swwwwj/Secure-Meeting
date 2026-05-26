"""
Validate id_card dataset before training.

  python -m training.validate_id_card_dataset
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TRAINING_DIR = Path(__file__).resolve().parent
DEFAULT_DATA_YAML = TRAINING_DIR / "dataset_id_card.yaml"
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def _load_dataset_root(data_yaml: Path) -> Path:
    path_value = ""
    for line in data_yaml.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("path:"):
            path_value = stripped.split(":", 1)[1].strip()
            break
    if not path_value:
        raise ValueError(f"No 'path:' in {data_yaml}")
    root = Path(path_value)
    if not root.is_absolute():
        root = (data_yaml.parent / root).resolve()
    return root


def _check_split(root: Path, split: str) -> tuple[int, list[str]]:
    errors: list[str] = []
    images_dir = root / "images" / split
    labels_dir = root / "labels" / split
    if not images_dir.is_dir():
        return 0, [f"Missing directory: {images_dir}"]

    count = 0
    for image_path in sorted(images_dir.iterdir()):
        if not image_path.is_file() or image_path.suffix.lower() not in IMAGE_EXTS:
            continue
        count += 1
        label_path = labels_dir / f"{image_path.stem}.txt"
        if not label_path.is_file():
            errors.append(f"{split}: missing label for {image_path.name}")
            continue
        lines = [ln.strip() for ln in label_path.read_text(encoding="utf-8").splitlines() if ln.strip()]
        if not lines:
            errors.append(f"{split}: empty label {label_path.name}")
            continue
        for line_no, line in enumerate(lines, start=1):
            parts = line.split()
            if len(parts) != 5:
                errors.append(f"{split}: {label_path.name}:{line_no} expected 5 fields, got {len(parts)}")
                continue
            cls_id = int(parts[0])
            if cls_id != 0:
                errors.append(f"{split}: {label_path.name}:{line_no} class must be 0 (id_card), got {cls_id}")
            coords = [float(x) for x in parts[1:]]
            if any(c < 0 or c > 1 for c in coords):
                errors.append(f"{split}: {label_path.name}:{line_no} coords must be in [0,1]")
            w, h = coords[2], coords[3]
            if w <= 0 or h <= 0:
                errors.append(f"{split}: {label_path.name}:{line_no} width/height must be > 0")
    return count, errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate id_card YOLO dataset.")
    parser.add_argument("--data", default=str(DEFAULT_DATA_YAML), help="Dataset yaml path.")
    args = parser.parse_args()

    data_yaml = Path(args.data).resolve()
    if not data_yaml.is_file():
        print(f"Not found: {data_yaml}", file=sys.stderr)
        return 1

    root = _load_dataset_root(data_yaml)
    print(f"Dataset root: {root}")

    train_n, train_err = _check_split(root, "train")
    val_n, val_err = _check_split(root, "val")
    errors = train_err + val_err

    print(f"Train images: {train_n}")
    print(f"Val images  : {val_n}")

    if train_n == 0:
        errors.append("No training images. Add data or run prepare_id_card_dataset --source ...")

    if errors:
        print("\nErrors:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    if train_n < 50:
        print(f"\nWarning: only {train_n} train images. Recommend 300+ for stable id_card detection.")
    print("\nDataset OK. Train with: python -m training.train_yolo")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
