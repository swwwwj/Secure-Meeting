# Adversarial perturbation weights (Phase E)

Default weights shipped in repo:

`ai_service/models/perturbation/best.pt`

Re-export after training (optional):

`ai_service/training/runs/adversarial_perturbation/weights/best.pt`

Train (CPU only, no GPU):

```bash
cd ai_service
python -m training.train_adversarial_perturbation
```

Exports sparse pixel-delta weights (`pixel_delta_v1`), not a heavy neural model.

Then set `perturbation_provider` to `learned` in `config/dev.json`.
