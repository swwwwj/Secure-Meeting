# ArcFace Multi-Face Feature Guide

## 功能概览

本次功能实现围绕“登录后录入命名人脸、入会前选择可见人脸、会议中对白名单外人脸自动打码”展开，并补充了多人脸场景的稳定性和易用性优化。

当前已支持：

- 登录后从本地图片录入一个或多个人脸照片
- 为每张录入照片设置自定义名称
- 一个账号保存多张命名人脸资料
- 入会前在 Join 页面按“命名人脸条目”勾选允许显示的人脸
- 会议中对未勾选的人脸自动打码
- 会议内从当前画面一次性批量录入多张检测到的人脸
- 会议中动态勾选/取消勾选人脸条目，实时切换是否模糊
- 多人同时出现时正确区分白名单与非白名单
- 优化会议界面布局，提升视频显示区域高度

## 实现逻辑

### 1. 数据模型升级

原始方案仅支持“一个用户一张人脸”，无法满足一个账号保存多张命名脸的需求。

本次升级后：

- `meeting_server` 新增 `face_profile_entries` 表
- 每条记录包含：
  - `user_id`
  - `profile_name`
  - `image_base64`
  - `created_at`
  - `updated_at`
- 同一用户可保存多条命名人脸
- 白名单单位由“用户名”升级为“人脸条目键值”

人脸条目键值格式：

```text
username::label
```

例如：

```text
alice::正脸
alice::侧脸
```

### 2. Join 页面录入逻辑

Join 页面支持一次选择多张图片：

- 单张图片时：
  - 若填写“人脸命名”，则直接使用该名称
  - 若未填写，则退回使用文件名
- 多张图片时：
  - 若填写“人脸命名”，自动生成 `命名-1`、`命名-2`
  - 若未填写，则使用各自文件名

客户端将每张图片作为一条命名人脸上传到：

```text
/api/v1/face-profiles/me
```

服务端根据 `label + user_id` 做新增或更新。

### 3. 入会前白名单逻辑

Join 页面展示的不再是单纯“用户名列表”，而是“命名人脸条目列表”。

例如：

```text
alice / 正脸
alice / 侧脸
bob / 工位照
```

用户勾选的是具体人脸条目，而不是整个人。

入会时客户端把这些勾选条目的 `profile_key` 发送给 AI 处理链路，作为不模糊白名单。

### 4. 会议内批量录入逻辑

会议内点击“录入当前画面人脸”后：

- 客户端会抓取当前视频帧
- 调用 `ai_service` 新增接口：

```text
/api/v1/face/enroll_many
```

- `ai_service` 检测当前帧中的所有人脸
- 对每张人脸提取 embedding
- 生成临时会议内人脸条目，例如：

```text
live::request-id::1
live::request-id::2
```

- 客户端把这些条目加入会议右侧列表，默认勾选

这样在多人同框时可以一次录入多个人脸，再逐个决定是否显示。

### 5. 人脸识别与模糊逻辑

AI 服务采用 ArcFace/InsightFace 链路：

1. 检测当前画面中的所有人脸
2. 为每张检测结果绑定自己的 embedding
3. 与当前会议的白名单人脸模板做匹配
4. 匹配成功的人脸保持清晰
5. 未命中的人脸进入打码列表

本次还修复了此前多人脸下“错误复用最高置信度人脸 embedding”的问题，避免白名单与非白名单一起被误打码。

### 6. 性能优化

为降低延迟和卡顿，本次实现包含以下优化：

- 客户端上传到 AI 服务前缩放图像最长边到 640
- 优先使用 JPEG 传输，降低编码和网络开销
- ArcFace 开启时限制并发处理请求，减少积压
- AI 返回处理帧后，优先保持处理后的预览结果
- 人脸处理链路在跳帧时复用上一次的匹配结果

### 7. 会议界面布局优化

会议页从原来的纵向堆叠布局调整为：

- 左侧：主视频网格
- 右侧：AI Protection 与 ArcFace 控制侧边栏
- 底部：媒体控制条

这样视频区域获得更多垂直空间，单人和多人场景下画面显示更完整。

## 需要同步到远程仓库的文件

以下文件属于本次功能实现的一部分，应同步到远程仓库：

### AI 服务

- `ai_service/app.py`
- `ai_service/face_pipeline.py`
- `ai_service/tests/test_process_frame_api.py`

### 客户端控制器与服务

- `client/controller/MainController.cpp`
- `client/controller/MainController.h`
- `client/services/AIProcessor.h`
- `client/services/HttpAIProcessor.cpp`
- `client/services/HttpAIProcessor.h`
- `client/services/HttpUserService.cpp`
- `client/services/HttpUserService.h`
- `client/services/MockUserService.cpp`
- `client/services/MockUserService.h`
- `client/services/UserService.h`

### 客户端界面

- `client/ui/JoinMeetingWindow.cpp`
- `client/ui/JoinMeetingWindow.h`
- `client/ui/MeetingWindow.cpp`
- `client/ui/MeetingWindow.h`
- `client/ui/VideoWidget.cpp`

### 会议服务端

- `meeting_server/app.py`
- `meeting_server/models.py`
- `meeting_server/schemas.py`
- `meeting_server/tests/test_meeting_server_mvp.py`
- `meeting_server/alembic/versions/0003_face_profiles.py`
- `meeting_server/alembic/versions/0004_multi_face_profiles.py`

### 文档

- `documents/arcface-feature-guide.md`

## 不应同步到远程仓库的本地生成文件

以下内容为本地环境产物或调试产物，不建议提交：

- `.venv311/`
- `build-macos/`
- `.dbg/`
- `meeting_server/meeting_server_dev.db`
- `ai_service/models/arcface/models/`

## 部署指导

### 1. 安装 Python 依赖

在仓库根目录已有 `.venv311` 的前提下，确保依赖安装完整。

#### meeting_server

```bash
cd /Users/zzzjy/Project/RuanGProject/Secure-Meeting/meeting_server
../.venv311/bin/pip install -r requirements.txt
```

#### ai_service

```bash
cd /Users/zzzjy/Project/RuanGProject/Secure-Meeting/ai_service
../.venv311/bin/pip install -r requirements.txt
../.venv311/bin/pip install insightface onnxruntime
```

### 2. 执行数据库迁移

本次功能依赖 `0003_face_profiles` 与 `0004_multi_face_profiles`。

```bash
cd /Users/zzzjy/Project/RuanGProject/Secure-Meeting/meeting_server
../.venv311/bin/alembic upgrade head
```

### 3. 启动 meeting_server

```bash
cd /Users/zzzjy/Project/RuanGProject/Secure-Meeting/meeting_server
../.venv311/bin/uvicorn app:app --host 127.0.0.1 --port 8100
```

### 4. 启动 ai_service

不要再使用 `SM_FACE_PROVIDER=mock`。

```bash
cd /Users/zzzjy/Project/RuanGProject/Secure-Meeting/ai_service
unset SM_FACE_PROVIDER
unset SM_DETECTOR_PROVIDER
../.venv311/bin/uvicorn app:app --host 127.0.0.1 --port 8000
```

首次运行 InsightFace 时，会自动下载 ArcFace 模型到：

```text
ai_service/models/arcface/models/
```

该目录是运行时下载结果，不建议提交。

### 5. 构建并启动客户端

```bash
cd /Users/zzzjy/Project/RuanGProject/Secure-Meeting
cmake --build build-macos --target SecureMeetingClient -j4
open /Users/zzzjy/Project/RuanGProject/Secure-Meeting/build-macos/client/SecureMeetingClient.app
```

如果需要从 `/Applications` 打开：

```bash
cp -R /Users/zzzjy/Project/RuanGProject/Secure-Meeting/build-macos/client/SecureMeetingClient.app /Applications/
open /Applications/SecureMeetingClient.app
```

## 验收建议

### 验证 Join 页

- 登录后一次选择多张照片上传
- 为照片输入命名
- 确认列表中出现多个命名人脸条目
- 勾选部分条目加入会议

### 验证会议页

- 让两人或多人同时出现在镜头中
- 点击“录入当前画面人脸”
- 确认右侧列表一次新增多个人脸条目
- 勾选/取消勾选不同条目，观察对应人脸是否清晰或打码

### 验证多人匹配

- 白名单与非白名单同时出现
- 确认只有未勾选条目对应的人脸被模糊
- 确认多人场景下延迟较之前更稳定
