# Frontend Integration Architecture

## 1. Overview

The standalone client-side JavaScript simulation (`app.js`) has been completely removed and replaced with a **React + TypeScript + Vite + Tailwind CSS** administration console located under `frontend/`.

- **Single Source of Truth:** All node health, transaction state, lock tables, Wait-For Graphs, 2PC state, WAL entries, recovery reports, and key-value records come exclusively from the C++ `dlm_api` backend.
- **Authentication:** First-run setup (`POST /api/auth/setup`) or login (`POST /api/auth/login`) stores a short-lived HMAC-signed bearer token in `sessionStorage` and authenticates both REST calls (`Authorization: Bearer`) and WebSocket upgrades (`/ws?token=...`).
- **Snapshot + Streaming Sync:** On mount and on every WebSocket reconnect, `App.tsx` fetches `GET /api/cluster` to restore complete cluster state, supplemented by live WebSocket events and periodic 3-second consistency refreshes.

## 2. Dashboard Pages

1. **Dashboard Overview** (`DashboardPage.tsx`)
2. **Database Explorer** (`ExplorerPage.tsx`)
3. **SQL Command Console** (`SqlConsolePage.tsx`)
4. **Transaction Monitor** (`TransactionsPage.tsx`)
5. **Lock Manager** (`LocksPage.tsx`)
6. **Deadlock Visualizer** (`DeadlocksPage.tsx`)
7. **Cluster Topology** (`TopologyPage.tsx`)
8. **Node Details** (`NodeDetailsPage.tsx`)
9. **2PC Monitor** (`TwoPCPage.tsx`)
10. **WAL Viewer** (`WalPage.tsx`)
11. **Recovery Monitor** (`RecoveryPage.tsx`)
12. **Logs** (`LogsPage.tsx`)
13. **Metrics** (`MetricsPage.tsx`)
14. **Settings** (`SettingsPage.tsx`)
