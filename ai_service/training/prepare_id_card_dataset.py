"""
Prepare YOLO dataset directories and split raw images + labels for id_card.

Raw folder layout (each image must have a matching .txt label):
  raw/
    img001.jpg
    img001.txt
    img002.png
    img002.txt

Label format (one line per object, class 0 = id_card):
  0 0.5 0.5 0.3 0.2

Commands (from ai_service/):
  python -m training.prepare_id_card_dataset --init
  python -m training.prepare_id_card_dataset --source D:/data/id_card_raw --val-ratio 0.2
"""

from __future__ import annotations

import argparse
import random
import shutil
import sys
from pathlib import Path

TRAINING_DIR = Path(__file__).resolve().parent
DATASET_ROOT = TRAINING_DIR / "datasets" / "id_card"
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def _ensure_layout(root: Path) -> None:
    for split in ("train", "val"):
        (root / "images" / split).mkdir(parents=True, exist_ok=True)
        (root / "labels" / split).mkdir(parents=True, exist_ok=True)


def _collect_pairs(source: Path) -> list[tuple[Path, Path]]:
    pairs: list[tuple[Path, Path]] = []
    for image_path in sorted(source.iterdir()):
        if not image_path.is_file():
            continue
        if image_path.suffix.lower() not in IMAGE_EXTS:
            continue
        label_path = image_path.with_suffix(".txt")
        if not label_path.is_file():
            print(f"Skip (no label): {image_path.name}", file=sys.stderr)
            continue
        pairs.append((image_path, label_path))
    return pairs


def _copy_pair(image_path: Path, label_path: Path, images_dir: Path, labels_dir: Path) -> None:
    shutil.copy2(image_path, images_dir / image_path.name)
    shutil.copy2(label_path, labels_dir / label_path.name)


def cmd_init(_: argparse.Namespace) -> int:
    _ensure_layout(DATASET_ROOT)
    readme = DATASET_ROOT / "README.txt"
    readme.write_text(
        "Put training data via prepare_id_card_dataset --source <raw_folder>\n"
        "Or manually copy into images/train, labels/train, images/val, labels/val.\n"
        "Class id in .txt files must be 0 (id_card).\n",
        encoding="utf-8",
    )
    print(f"Created dataset layout at: {DATASET_ROOT}")
    return 0


def cmd_split(args: argparse.Namespace) -> int:
    source = Path(args.source).resolve()
    if not source.is_dir():
        print(f"Source not found: {source}", file=sys.stderr)
        return 1

    pairs = _collect_pairs(source)
    if not pairs:
        print(f"No image+label pairs in {source}", file=sys.stderr)
        return 1

    _ensure_layout(DATASET_ROOT)
    if args.clear:
        for split in ("train", "val"):
            for sub in ("images", "labels"):
                target = DATASET_ROOT / sub / split
                for item in target.iterdir():
                    if item.is_file():
                        item.unlink()

    random.seed(args.seed)
    random.shuffle(pairs)
    val_count = max(1, int(len(pairs) * args.val_ratio)) if len(pairs) >= 2 else 0
    if len(pairs) == 1:
        val_count = 0
    val_set = set(range(len(pairs) - val_count, len(pairs)))

    train_n = val_n = 0
    for idx, (image_path, label_path) in enumerate(pairs):
        split = "val" if idx in val_set else "train"
        images_dir = DATASET_ROOT / "images" / split
        labels_dir = DATASET_ROOT / "labels" / split
        _copy_pair(image_path, label_path, images_dir, labels_dir)
        if split == "train":
            train_n += 1
        else:
            val_n += 1

    print(f"Copied {train_n} train + {val_n} val samples into {DATASET_ROOT}")
    print("Next: python -m training.validate_id_card_dataset")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare id_card YOLO dataset.")
    parser.add_argument("--init", action="store_true", help="Create empty train/val folder layout.")
    parser.add_argument("--source", help="Raw folder with paired image + .txt labels.")
    parser.add_argument("--val-ratio", type=float, default=0.2, help="Fraction for validation split.")
    parser.add_argument("--seed", type=int, default=42, help="Shuffle seed for split.")
    parser.add_argument("--clear", action="store_true", help="Clear existing train/val files before copy.")
    args = parser.parse_args()

    if args.init:
        return cmd_init(args)
    if args.source:
        return cmd_split(args)

    parser.print_help()
    print("\nUse --init or --source <dir>", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
