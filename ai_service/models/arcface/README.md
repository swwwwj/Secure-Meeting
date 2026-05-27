# ArcFace 预训练模型目录（Phase D）

将 InsightFace 预训练包放在此目录下，供 `face_provider=insightface` 使用。

## 推荐方式（自动下载）

安装依赖后，首次启动 AI 服务并调用识别接口时，InsightFace 会在此目录下载 `buffalo_l` 模型：

```powershell
pip install insightface onnxruntime
```

目录结构示例（自动生成）：

```
models/arcface/
  models/
    buffalo_l/
      det_10g.onnx
      w600k_r50.onnx
      ...
```

## 配置

在 `config/dev.json` 中：

```json
"face_provider": "insightface",
"arcface_model_dir": "models/arcface"
```

## 无模型时

- `face_provider=mock`：测试与无 GPU/无模型环境（默认 test 环境）
- `insightface` 加载失败时，人脸隐私链路自动降级（不模糊、不中断会议）

## 许可说明

请遵守 InsightFace / 所下载权重的开源许可，仅用于课程与本地研发。
