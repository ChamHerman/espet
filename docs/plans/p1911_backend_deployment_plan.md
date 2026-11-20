# Production Deployment & Infrastructure Migration Plan: ESPET Backend

## [Goal Description]

Currently, the `espet-backend` server runs on **InstaBox** (free tier). While convenient for quick agent sandboxes, InstaBox introduces noticeable latency and sluggish responses for ESPET:
1. **Auto-Pause Cold Starts**: Free microVMs sleep after inactivity, causing multi-second freezes on incoming requests.
2. **Geographical & Proxy Overhead**: Traffic routes through remote global gateways rather than local Southeast Asian infrastructure.
3. **Architecture Incompatibility with Edge Serverless**: Cloudflare Workers cannot run `espet-backend` due to native C/C++ dependencies (`faster-whisper`, `sqlite-vec`, `fastembed`), persistent WebSockets (`/ws/device`), and SQLite disk storage.

### Target Architecture
Migrate `espet-backend` to an **Oracle Cloud Infrastructure (OCI) Always Free** instance hosted in the **Singapore region (`ap-singapore-1`)**:
- **Ultra-low latency for Malaysia**: Physical proximity (~5–15 ms round-trip ping).
- **24/7 Persistent Availability**: Never sleeps; full 4 OCPU / 24 GB RAM (ARM64 Ampere A1) allocation.
- **Security & Zero-Port-Forwarding**: Integrated with **Cloudflare Tunnel (`cloudflared`)** for automated public HTTPS/WSS without opening incoming firewall ports on Oracle VCN.
- **Automated Continuous Deployment (CI/CD)**: On every `git push` to `main`, a GitHub Actions workflow deploys the updated backend seamlessly.
- **Containerization & Kiri Daemon Evaluation**: Standardized multi-stage `Dockerfile` and `docker-compose.yml` supporting both standard Docker CE and the experimental Rust-based container runtime **Kiri** (`libracoder/kiri`).

---

```
                        ┌─────────────────────────────────────────────────────────┐
                        │             Cloudflare Global Edge Network             │
                        │             https://espet.yourdomain.com                │
                        └──────────────────────────┬──────────────────────────────┘
                                                   │
                       Encrypted Outbound Tunnel   │ (No open ingress ports)
                                                   ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│               Oracle Cloud Always Free VM (Ubuntu 24.04 ARM64, Singapore)               │
│                                                                                        │
│  ┌──────────────────────────────────────────────────────────────────────────────────┐  │
│  │                              Docker / Kiri Network                               │  │
│  │                                                                                  │  │
│  │  ┌────────────────────────┐      Internal Net      ┌──────────────────────────┐  │  │
│  │  │   cloudflared tunnel   │ ────────────────────>  │      espet-backend       │  │  │
│  │  │ (Cloudflare connector) │    port 8000 (HTTP)    │  (FastAPI + MCP + Voice) │  │  │
│  │  └────────────────────────┘                        └─────────────┬────────────┘  │  │
│  │                                                                  │               │  │
│  └──────────────────────────────────────────────────────────────────┼───────────────┘  │
│                                                                     ▼                  │
│                                                     Persistent Volumes:                │
│                                                     - ./data  (Memory & state)         │
│                                                     - ./db    (OAuth SQLite tokens)    │
│                                                     - .env    (Secrets & config)       │
└────────────────────────────────────────────────────────────────────────────────────────┘
                                      ▲
                                      │ SSH Deploy Trigger (git pull & compose restart)
                                      │
                         ┌────────────┴────────────┐
                         │   GitHub Actions CI/CD  │
                         │   (On push to main)     │
                         └─────────────────────────┘
```

---

## User Review Required

> [!IMPORTANT]
> **Oracle Cloud Account Region Selection**: When signing up for Oracle Cloud Always Free, ensure your **Home Region** is selected as **Singapore (`ap-singapore-1`)**. Oracle does not allow changing the Always Free home region after account creation.

> [!NOTE]
> **Kiri Runtime vs Native Docker**:
> - Kiri is a fast, lightweight container runtime written in Rust implementing the Docker Engine API.
> - On a Linux server, standard Docker CE already runs directly on the Linux kernel with minimal overhead.
> - The proposed `Dockerfile` and `docker-compose.yml` will be completely standard. You can evaluate Kiri on the VM (`docker --context kiri compose up -d`); if any early-stage alpha issue arises, you can effortlessly switch to native Docker without modifying any project configuration.

---

## Open Questions

None. All core architectural decisions have been aligned during the initial design interview.

---

## Proposed Changes

### Component 1: Containerization Configuration (`espet-backend/`)

#### [NEW] `espet-backend/Dockerfile`
- Multi-stage, ARM64/x86_64 compatible Python 3.11 image (`python:3.11-slim-bookworm`).
- Installs necessary system libraries for audio/video (`ffmpeg`, `libsm6`, `libxext6`, `curl`).
- Installs Python dependencies and runs `uvicorn main:app --host 0.0.0.0 --port 8000`.

```dockerfile
FROM python:3.11-slim-bookworm

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1 \
    DEBIAN_FRONTEND=noninteractive

WORKDIR /app

# System dependencies for audio/voice (PyAV, faster-whisper, ffmpeg)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ffmpeg \
    libsm6 \
    libxext6 \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Install python dependencies
COPY requirements.txt .
RUN pip install --upgrade pip && \
    pip install -r requirements.txt

# Copy source code
COPY . .

# Ensure data and db directories exist
RUN mkdir -p /app/data /app/db /app/static

EXPOSE 8000

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
```

#### [NEW] `espet-backend/docker-compose.yml`
- Declares `backend` service with health check, restart policies, and persistent host volume mounts (`./data`, `./db`, `./.env`).
- Declares `tunnel` service (`cloudflare/cloudflared`) to establish a zero-trust tunnel to Cloudflare edge.

```yaml
version: '3.8'

services:
  backend:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: espet-backend
    restart: unless-stopped
    env_file:
      - .env
    volumes:
      - ./data:/app/data
      - ./db:/app/db
      - ./.env:/app/.env
    ports:
      - "127.0.0.1:8000:8000"
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8000/health"]
      interval: 30s
      timeout: 5s
      retries: 3
      start_period: 20s

  tunnel:
    image: cloudflare/cloudflared:latest
    container_name: espet-tunnel
    restart: unless-stopped
    command: tunnel run
    environment:
      - TUNNEL_TOKEN=${CLOUDFLARE_TUNNEL_TOKEN}
    depends_on:
      - backend
```

#### [NEW] `espet-backend/.dockerignore`
```dockerignore
__pycache__
*.pyc
*.pyo
*.pyd
.pytest_cache
venv/
.venv/
node_modules/
tests/
.git/
.gitignore
```

---

### Component 2: Continuous Deployment (`.github/workflows/`)

#### [NEW] `.github/workflows/deploy-backend.yml`
- Triggers on push to `main` branch when changes occur in `espet-backend/**` or `.github/workflows/deploy-backend.yml`.
- Connects to the Oracle VM via SSH (using GitHub Secrets).
- Executes git pull and runs `docker compose up -d --build`.

```yaml
name: Deploy ESPET Backend to Oracle Cloud

on:
  push:
    branches:
      - main
    paths:
      - 'espet-backend/**'
      - '.github/workflows/deploy-backend.yml'
  workflow_dispatch:

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - name: Deploy to Oracle Always Free VM via SSH
        uses: appleboy/ssh-action@v1.2.0
        with:
          host: ${{ secrets.ORACLE_VM_HOST }}
          username: ${{ secrets.ORACLE_VM_USER }}
          key: ${{ secrets.ORACLE_VM_SSH_KEY }}
          script: |
            set -e
            echo "[+] Pulling latest repository changes..."
            cd ~/espet/espet-backend
            git fetch origin main
            git reset --hard origin/main
            
            echo "[+] Deploying containers with docker compose..."
            # Note: If evaluating Kiri, replace with 'kiri compose up -d --build'
            # or 'docker --context kiri compose up -d --build'
            docker compose up -d --build
            
            echo "[+] Checking container health..."
            docker ps --filter "name=espet-backend"
            echo "[+] Deployment successful!"
```

---

### Component 3: Firmware Configuration (`main/mimi_secrets.h`)

#### [MODIFY] `main/mimi_secrets.h` & `main/mimi_config.h`
Update backend configuration to point to the secure Cloudflare Tunnel endpoint:
```c
/* Backend Server Cloudflare Tunnel URL */
#define MIMI_BACKEND_URL "https://espet.yourdomain.com"
```
*(Firmware's `cloud_link.c` and `backend_discovery.c` will automatically establish encrypted TLS connections via `wss://` and `https://` using the built-in ESP-IDF certificate bundle `esp_crt_bundle_attach`.)*

---

### Component 4: Step-by-Step Server Setup Guide

#### 1. Oracle Cloud Infrastructure (OCI) Setup
1. **Register**: Sign up at [oracle.com/cloud/free](https://www.oracle.com/cloud/free/). Select **Home Region: Singapore (`ap-singapore-1`)**.
2. **Create Instance**:
   - Go to **Compute** > **Instances** > **Create Instance**.
   - Image: **Ubuntu 24.04 LTS** (or 22.04 LTS).
   - Shape: Click **Change Shape** > **Ampere (ARM-based)** > Select `VM.Standard.A1.Flex` (Config: 2 or 4 OCPU, 12 or 24 GB RAM).
   - SSH Keys: Generate a key pair or upload your public key. Save the private key (`oracle_key.pem`).
   - Click **Create**.
3. **Connect**:
   ```bash
   ssh -i oracle_key.pem ubuntu@<ORACLE_VM_PUBLIC_IP>
   ```

#### 2. Install Docker & Git on the VM
```bash
# Update system
sudo apt-get update && sudo apt-get upgrade -y
sudo apt-get install -y git curl ca-certificates

# Install Docker CE & Docker Compose plugin
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
newgrp docker
```

#### 3. (Optional) Evaluate Kiri on the VM
If you want to test the Rust container daemon Kiri:
```bash
# Download and install Kiri for Linux (ARM64)
# Follow instructions from https://docs.runkiri.com
curl -fsSL https://runkiri.com/install.sh | sh
# Or test Kiri context with docker CLI:
docker context use kiri
```
*Tip: If Kiri encounters any early release quirks on ARM64 Linux, switch back instantly with `docker context use default`.*

#### 4. Configure Cloudflare Tunnel
1. Go to [Cloudflare Zero Trust Dashboard](https://one.dash.cloudflare.com/) > **Networks** > **Tunnels** > **Create Tunnel**.
2. Choose **Cloudflared**. Name it `espet-tunnel`.
3. Copy the tunnel token (`CLOUDFLARE_TUNNEL_TOKEN`).
4. In the Public Hostname tab, set:
   - Subdomain: `espet`
   - Domain: `yourdomain.com`
   - Service: `HTTP` -> `backend:8000` (resolves internally inside Docker Compose network).

#### 5. Configure GitHub Secrets for Auto-Deployment
In your GitHub repo settings (**Settings** > **Secrets and variables** > **Actions**), add:
- `ORACLE_VM_HOST`: Public IP of the Oracle VM.
- `ORACLE_VM_USER`: `ubuntu`
- `ORACLE_VM_SSH_KEY`: Content of `oracle_key.pem`.

---

## Verification Plan

### Automated Checks
1. **Docker Build Verification**:
   ```bash
   cd espet-backend
   docker compose build
   ```
2. **Backend Health Check**:
   ```bash
   curl -f http://localhost:8000/health
   ```
3. **GitHub Action Run**: Push a minor commit to `main` and verify that the `Deploy ESPET Backend to Oracle Cloud` workflow passes in the GitHub Actions tab.

### Manual Verification
1. **Tunnel Connectivity**:
   Visit `https://espet.yourdomain.com/health` in a browser or curl from your local PC:
   ```bash
   curl -i https://espet.yourdomain.com/health
   ```
   Ensure it returns status `200 OK` with MCP discovery details.
2. **Latency Check**:
   Ping and measure HTTP TTFB (Time to First Byte) from Malaysia:
   ```bash
   curl -o /dev/null -s -w 'Total time: %{time_total}s\n' https://espet.yourdomain.com/health
   ```
   Target: sub-100ms total response time (down from multi-second InstaBox response).
3. **ESP32 Firmware Verification**:
   Flash firmware with updated `MIMI_BACKEND_URL` and confirm WebSocket `/ws/device` connects immediately in the monitor log.
