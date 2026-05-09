# Secure Meeting

## 1. Overview

Secure Meeting is a video processing and conferencing system with privacy protection capabilities.
The system enhances video streams using computer vision and adversarial perturbation techniques to protect user identity and sensitive information.

The system is designed to be built incrementally, starting from a single-machine pipeline and later extending to real-time communication.

---

## 2. System Architecture

The system is composed of three independent components:

```id="arch1"
Client (C++)
    ↔ AI Service (Python)
    ↔ Meeting Server (Python, optional in early stage)
```

### Design Principles

* Real-time tasks stay in the client
* Heavy computation is handled by AI service
* All components communicate via clear APIs
* Each component must be runnable independently

---

## 3. Development Strategy (Critical)

The system must be implemented in phases:

### Phase 1 (MVP - REQUIRED FIRST)

Single-machine pipeline:

```id="flow1"
Camera → Client → AI Service → Client Display
```

No networking, no RTC.

---

### Phase 2

* Add detection (YOLO)
* Add face recognition (ArcFace)
* Add ROI extraction

---

### Phase 3

* Add adversarial perturbation
* Improve processing pipeline

---

### Phase 4 (Optional / Advanced)

* Add RTC communication
* Add meeting server

---

## 4. Responsibilities

### 4.1 Client (C++)

Responsibilities:

* Capture frames from camera
* Send frames to AI service
* Receive processed frames
* Display video

Do NOT implement:

* YOLO
* ArcFace
* Adversarial algorithms

---

### 4.2 AI Service (Python)

Responsibilities:

* Receive image/frame
* Run detection / recognition
* Apply perturbation
* Return processed frame

Must expose HTTP API.

---

### 4.3 Meeting Server (Python)

Responsibilities:

* Room management
* Signaling

This module is NOT required in Phase 1.

---

## 5. API Specification (MANDATORY)

### AI Service API

#### Endpoint

```
POST /process_frame
POST /api/v1/process_frame
```

#### Request

* Content-Type: application/json or multipart/form-data

Example (base64):

```json
{
  "image": "<base64_encoded_image>",
  "request_id": "optional-client-request-id",
  "trace_id": "optional-trace-id",
  "model_version": "optional-override",
  "policy_version": "optional-override"
}
```

---

#### Response

```json
{
  "request_id": "<resolved_request_id>",
  "trace_id": "<resolved_trace_id>",
  "model_version": "<resolved_model_version>",
  "policy_version": "<resolved_policy_version>",
  "image": "<base64_encoded_processed_image>",
  "latency_ms": 12.34
}
```

Legacy compatibility:

```json
POST /process_frame
{"image": "..."}
-> {"image": "..."}
```

---

### Requirements

* Must support single frame processing
* Must be stateless
* Response time should be minimized

---

## 6. Data Flow (MVP)

```id="flow2"
1. Capture frame (C++)
2. Encode to base64
3. Send to AI service (HTTP)
4. AI processes frame
5. Return processed image
6. Decode and display
```

---

## 7. Project Structure

```id="struct1"
secure-meeting/
│
├── client/
│   ├── capture/
│   ├── network/
│   ├── display/
│   └── main.cpp
│
├── ai_service/
│   ├── api/
│   ├── detection/
│   ├── face/
│   ├── perturbation/
│   └── app.py
│
├── server/         # optional
│
└── README.md
```

---

## 8. Implementation Requirements

### General

* Each module must compile/run independently
* Avoid monolithic code
* Use clear interfaces

---

### Client

* Use OpenCV for video capture
* Use HTTP client library to call AI service

---

### AI Service

* Use FastAPI
* Use OpenCV for image handling
* Use PyTorch for models (later stages)

---

## 9. Performance Targets

* Target FPS: 20–30 (Phase 1 acceptable ≥15)
* Processing latency: as low as possible
* System must not block UI thread

---

## 10. Constraints

* Do not implement all features at once
* Do not introduce RTC before pipeline works
* Do not tightly couple client and AI logic
* Keep modules replaceable

---

## 11. Minimal Deliverable (Definition of Done)

Phase 1 is complete when:

* Camera feed is displayed
* Frames are sent to AI service
* AI service returns modified frames
* Processed video is shown in real-time

---

## 12. Extension Goals

After MVP:

* Add YOLO detection
* Add ArcFace recognition
* Add adversarial perturbation
* Add RTC communication
* Add multi-user support

---

## Phase A Productization Notes

- Environment config:
  - `ai_service/config/dev.json|test.json|prod.json`
  - `client/config/dev.json|test.json|prod.json`
  - override via env: `SM_ENV`, `SM_AI_ENDPOINT`, `SM_MODEL_VERSION`, `SM_POLICY_VERSION`
- Observability:
  - structured JSON logs on client and AI service
  - local metrics endpoint: `GET /api/v1/metrics`
- Test:
  - run `python -m pytest` in `ai_service/`
  - covers success, invalid payload, forced error, delay path, and legacy compatibility

---

## Phase B Meeting Server MVP

- New module: `meeting_server/`
- Capabilities:
  - `POST /api/v1/auth/register`
  - `POST /api/v1/auth/login`
  - `POST /api/v1/auth/logout`
  - `POST /api/v1/rooms/create`
  - `POST /api/v1/rooms/join`
  - `POST /api/v1/rooms/leave`
  - `POST /api/v1/signaling/message` (placeholder)
  - `POST /api/v1/rooms/policy` (policy-change placeholder)
- Database:
  - Dev default uses SQLite (`sqlite:///./meeting_server_dev.db`), so local run does not require PostgreSQL
  - Test uses SQLite; Prod can use PostgreSQL by setting `SM_MEETING_DATABASE_URL`
  - Migration tool: Alembic (`meeting_server/alembic/`)
  - Initial migration creates: `users`, `rooms`, `participants`, `sessions`, `audit_logs`
  - Auth hardening migration adds: `users.password_hash`, `sessions.revoked_at`
- Run migration:
  - `cd meeting_server`
  - `alembic upgrade head`
- Run tests:
  - `python -m pytest`

- Dev startup (no PostgreSQL required):
  - `cd meeting_server`
  - `python -m alembic upgrade head`
  - PowerShell one-liner:
    - `$env:SM_ENV='dev'; python -m uvicorn app:app --host 127.0.0.1 --port 8100`
  - Optional switch to PostgreSQL:
    - `$env:SM_MEETING_DATABASE_URL='postgresql+psycopg://user:password@127.0.0.1:5432/secure_meeting'`

- Client real-service path:
  - default `client/config/dev.json` sets `"use_mock_services": false`
  - uses `meeting_server_endpoint` + `x-session-token` for room APIs
  - set `SM_USE_MOCK_SERVICES=true` to rollback to mock services
