# Docker Compose Deployment Guide

## 1. Architecture

`docker-compose.yml` defines five isolated services connected on an internal bridge network (`dlm_net`):

- `node1`, `node2`, `node3`: Independent `dlm_node` instances with dedicated persistent volumes (`node1_data`, `node2_data`, `node3_data`).
- `api`: C++ `dlm_api` HTTP/WebSocket gateway (`:8080`) with `api_data` volume.
- `frontend`: Nginx serving the production Vite React build (`:3000`) and proxying `/api/*` and `/ws` to `api:8080`.

## 2. Configuration

Copy `.env.example` to `.env` and set strong values before deploying:

```bash
cp .env.example .env
```

Optional environment variables for headless admin bootstrap:
- `DLM_AUTH_SECRET` (random 64-char hex signing secret)
- `DLM_ADMIN_USER` and `DLM_ADMIN_PASSWORD` (if omitted, use the browser's first-run setup UI)

## 3. Startup & Shutdown Commands

```bash
# Build and start all 5 services in background
docker compose up --build -d

# Check container status & healthchecks
docker compose ps

# Follow logs
docker compose logs -f api node1 node2 node3

# Ordinary shutdown (preserves persistent data volumes)
docker compose down

# Explicit demo data reset (deletes persistent volumes)
docker compose down -v
```

Open `http://localhost:3000` in your browser.
