"""
Fine-tune YOLO for id_card (身份证) detection.

Run from ai_service directory:
  python -m training.train_yolo
  python -m training.train_yolo --device 0 --epochs 100 --batch 16

After training, set in config/dev.json:
  yolo_model_path: training/runs/id_card/weights/best.pt
  sensitive_labels: ["id_card"]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TRAINING_DIR = Path(__file__).resolve().parent
AI_SERVICE_DIR = TRAINING_DIR.parent
DEFAULT_DATA = TRAINING_DIR / "dataset_id_card.yaml"
DEFAULT_PROJECT = TRAINING_DIR / "runs"
DEFAULT_NAME = "id_card"


def _resolve(path_str: str, base: Path | None = None) -> Path:
    path = Path(path_str)
    if path.is_absolute():
        return path.resolve()
    root = base or Path.cwd()
    return (root / path).resolve()


def _print_deploy_hint(best_pt: Path) -> None:
    rel = best_pt
    try:
        rel = best_pt.relative_to(AI_SERVICE_DIR)
    except ValueError:
        pass
    rel_posix = rel.as_posix()
    print("\n=== Training finished ===")
    print(f"Best weights: {best_pt}")
    print("\nUpdate ai_service/config/dev.json:")
    print(json.dumps(
        {
            "yolo_model_path": rel_posix,
            "model_version": "yolo-phase-c-v1-id-card-finetuned",
            "sensitive_labels": ["id_card"],
            "detector_provider": "yolo",
        },
        indent=2,
        ensure_ascii=False,
    ))
    print("\nRestart ai_service, enable AI in the client, and test with a real ID card in view.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fine-tune YOLO for id_card detection.")
    parser.add_argument(
        "--data",
        default=str(DEFAULT_DATA),
        help="Path to dataset yaml (default: training/dataset_id_card.yaml).",
    )
    parser.add_argument(
        "--model",
        default="yolov8n.pt",
        help="Base checkpoint (yolov8n.pt / yolov8s.pt or path to .pt).",
    )
    parser.add_argument("--epochs", type=int, default=100, help="Training epochs.")
    parser.add_argument("--imgsz", type=int, default=640, help="Train/val image size.")
    parser.add_argument("--batch", type=int, default=16, help="Batch size (-1 = auto).")
    parser.add_argument("--device", default="cpu", help="Device: cpu, 0, 0,1, etc.")
    parser.add_argument("--project", default=str(DEFAULT_PROJECT), help="Output project directory.")
    parser.add_argument("--name", default=DEFAULT_NAME, help="Run name under project.")
    parser.add_argument("--patience", type=int, default=30, help="Early stopping patience.")
    parser.add_argument("--workers", type=int, default=4, help="Dataloader workers.")
    parser.add_argument("--seed", type=int, default=42, help="Random seed.")
    parser.add_argument("--resume", action="store_true", help="Resume last run in project/name.")
    parser.add_argument("--freeze", type=int, default=0, help="Freeze first N layers (0 = none).")
    args = parser.parse_args()

    data_yaml = _resolve(args.data, AI_SERVICE_DIR)
    if not data_yaml.is_file():
        print(f"Dataset config not found: {data_yaml}", file=sys.stderr)
        print("Run: python -m training.prepare_id_card_dataset --init", file=sys.stderr)
        return 1

    try:
        from ultralytics import YOLO  # type: ignore
    except ImportError as exc:
        print(f"Install ultralytics: pip install ultralytics ({exc})", file=sys.stderr)
        return 1

    project_dir = _resolve(args.project, AI_SERVICE_DIR)
    project_dir.mkdir(parents=True, exist_ok=True)

    print(f"Data config : {data_yaml}")
    print(f"Base model  : {args.model}")
    print(f"Output      : {project_dir / args.name}")
    print(f"Device      : {args.device}")

    model = YOLO(args.model)
    train_kwargs: dict = {
        "data": str(data_yaml),
        "epochs": args.epochs,
        "imgsz": args.imgsz,
        "batch": args.batch,
        "device": args.device,
        "project": str(project_dir),
        "name": args.name,
        "patience": args.patience,
        "workers": args.workers,
        "seed": args.seed,
        "exist_ok": True,
        "pretrained": True,
        "resume": args.resume,
        "verbose": True,
    }
    if args.freeze > 0:
        train_kwargs["freeze"] = args.freeze

    model.train(**train_kwargs)

    best_pt = project_dir / args.name / "weights" / "best.pt"
    if not best_pt.is_file():
        print(f"Warning: best.pt not found at {best_pt}", file=sys.stderr)
        return 2

    _print_deploy_hint(best_pt)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
