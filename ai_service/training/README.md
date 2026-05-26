# 身份证（id_card）YOLO 微调

单类别检测：`id_card`。与 `config/dev.json` 中 `sensitive_labels: ["id_card"]` 一致。

## 1. 准备数据

原始目录（每张图必须有同名 `.txt` 标注）：

```
raw/
  001.jpg
  001.txt
  002.jpg
  002.txt
```

标注格式（类别固定为 `0` = id_card，坐标归一化到 0~1）：

```
0 0.52 0.48 0.35 0.22
```

在 `ai_service` 目录执行：

```powershell
cd e:\Secure_Meeting\ai_service
python -m training.prepare_id_card_dataset --init
python -m training.prepare_id_card_dataset --source D:\your\raw_folder --val-ratio 0.2
python -m training.validate_id_card_dataset
```

## 2. 训练

```powershell
# CPU（慢）
python -m training.train_yolo

# 有 NVIDIA GPU 时
python -m training.train_yolo --device 0 --batch 16 --epochs 100
```

权重输出：`training/runs/id_card/weights/best.pt`

训练结束会打印需写入 `config/dev.json` 的字段。

## 3. 推理配置

`config/dev.json` 已预设：

- `yolo_model_path`: `training/runs/id_card/weights/best.pt`
- `sensitive_labels`: `["id_card"]`

训练完成后重启 ai_service，客户端打开 AI，对准身份证测试模糊效果。

## 4. 数据量建议

- 最少约 100 张图可试跑
- 稳定效果建议 300+ 张，多样光照、角度、手持/桌面场景

## 5. 标注工具

可用 [LabelImg](https://github.com/HumanSignal/labelImg)、Roboflow、CVAT 等，导出 YOLO 格式，类别名设为 `id_card`（对应 class id `0`）。
