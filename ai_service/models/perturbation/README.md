# Adversarial perturbation weights (Phase E)

Place trained generator weights here, or use the default training output path:

`ai_service/training/runs/adversarial_perturbation/weights/best.pt`

Train:

```bash
cd ai_service
pip install torch
python -m training.train_adversarial_perturbation --data ./training/perturbation_samples --epochs 30
```

Then set `perturbation_provider` to `learned` in `config/dev.json`.
