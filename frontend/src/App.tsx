import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  api,
  ApiError,
  getStoredToken,
  getWebSocketUrl,
  setStoredToken,
} from './api/client';
import { Layout, type ToastItem } from './components/Layout';
import { AuthPage } from './pages/AuthPage';
import { DashboardPage } from './pages/DashboardPage';
import { ExplorerPage } from './pages/ExplorerPage';
import { SqlConsolePage } from './pages/SqlConsolePage';
import { TransactionsPage } from './pages/TransactionsPage';
import { LocksPage } from './pages/LocksPage';
import { DeadlocksPage } from './pages/DeadlocksPage';
import { TopologyPage } from './pages/TopologyPage';
import { NodeDetailsPage } from './pages/NodeDetailsPage';
import { TwoPCPage } from './pages/TwoPCPage';
import { WalPage } from './pages/WalPage';
import { RecoveryPage } from './pages/RecoveryPage';
import { LogsPage } from './pages/LogsPage';
import { MetricsPage } from './pages/MetricsPage';
import { SettingsPage } from './pages/SettingsPage';
import type { AuthUser, ClusterSnapshot, PageId, SystemLog, WsEvent } from './types/dlm';

export const App: React.FC = () => {
  const [booting, setBooting] = useState(true);
  const [setupComplete, setSetupComplete] = useState(true);
  const [token, setToken] = useState<string | null>(() => getStoredToken());
  const [user, setUser] = useState<AuthUser | null>(null);

  const [activePage, setActivePage] = useState<PageId>('overview');
  const [darkTheme, setDarkTheme] = useState(true);
  const [snapshot, setSnapshot] = useState<ClusterSnapshot | null>(null);
  const [logs, setLogs] = useState<SystemLog[]>([]);
  const [events, setEvents] = useState<WsEvent[]>([]);
  const [wsConnected, setWsConnected] = useState(false);
  const [loadError, setLoadError] = useState('');
  const [toasts, setToasts] = useState<ToastItem[]>([]);

  const wsRef = useRef<WebSocket | null>(null);
  const toastSeq = useRef(1);

  useEffect(() => {
    document.documentElement.classList.toggle('dark', darkTheme);
  }, [darkTheme]);

  const notify = useCallback((kind: 'success' | 'error' | 'info', message: string) => {
    const id = toastSeq.current++;
    setToasts((prev) => [...prev, { id, kind, message }]);
    setTimeout(() => {
      setToasts((prev) => prev.filter((t) => t.id !== id));
    }, 4500);
  }, []);

  const fetchSnapshot = useCallback(async () => {
    if (!token) return;
    try {
      const [snap, logRes] = await Promise.all([api.getClusterSnapshot(), api.getLogs()]);
      setSnapshot(snap);
      setLogs(logRes.logs || []);
      setLoadError('');
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        setStoredToken(null);
        setToken(null);
        setUser(null);
      } else {
        setLoadError(err instanceof Error ? err.message : 'Failed to load cluster state.');
      }
    }
  }, [token]);

  // Initial auth bootstrap
  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const st = await api.authStatus();
        if (cancelled) return;
        setSetupComplete(st.setupComplete);
        if (token) {
          try {
            const me = await api.authMe();
            if (!cancelled) setUser(me.user);
          } catch {
            setStoredToken(null);
            if (!cancelled) setToken(null);
          }
        }
      } catch {
        // Backend may still be starting
      } finally {
        if (!cancelled) setBooting(false);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [token]);

  // WebSocket lifecycle + reconnect snapshot restore
  const connectWs = useCallback(() => {
    if (!token || !user) return;
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }

    const ws = new WebSocket(getWebSocketUrl(token));
    wsRef.current = ws;

    ws.onopen = () => {
      setWsConnected(true);
      // Restore full authoritative snapshot on connect / reconnect
      fetchSnapshot();
    };

    ws.onmessage = (evt) => {
      try {
        const ev = JSON.parse(evt.data as string) as WsEvent;
        setEvents((prev) => [ev, ...prev].slice(0, 150));
        fetchSnapshot();
      } catch {
        // Ignore malformed WS frame
      }
    };

    ws.onclose = () => {
      setWsConnected(false);
    };
  }, [token, user, fetchSnapshot]);

  useEffect(() => {
    if (!token || !user) return;
    fetchSnapshot();
    connectWs();

    const pollTimer = setInterval(() => {
      fetchSnapshot();
    }, 3000);

    return () => {
      clearInterval(pollTimer);
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, [token, user, fetchSnapshot, connectWs]);

  const handleLogout = async () => {
    try {
      await api.authLogout();
    } catch {
      // Ignore logout network error
    }
    setStoredToken(null);
    setToken(null);
    setUser(null);
    setSnapshot(null);
  };

  if (booting) {
    return (
      <div className="min-h-screen flex items-center justify-center bg-slate-950 text-slate-300 text-sm">
        Connecting to DLM-DB Backend...
      </div>
    );
  }

  if (!token || !user) {
    return (
      <AuthPage
        setupComplete={setupComplete}
        onAuthenticated={(tok, u) => {
          setSetupComplete(true);
          setToken(tok);
          setUser(u);
        }}
      />
    );
  }

  return (
    <Layout
      activePage={activePage}
      onNavigate={setActivePage}
      snapshot={snapshot}
      wsConnected={wsConnected}
      darkTheme={darkTheme}
      onToggleTheme={() => setDarkTheme((d) => !d)}
      onRefresh={fetchSnapshot}
      user={user}
      onLogout={handleLogout}
      toasts={toasts}
      onDismissToast={(id) => setToasts((prev) => prev.filter((t) => t.id !== id))}
    >
      {loadError && (
        <div className="mb-4 rounded-lg border border-red-500/40 bg-red-500/10 px-4 py-2.5 text-xs text-red-400">
          API Connection Issue: {loadError}
        </div>
      )}

      {!snapshot ? (
        <div className="py-16 text-center text-sm text-slate-500">
          Loading authoritative cluster snapshot from C++ backend...
        </div>
      ) : (
        <>
          {activePage === 'overview' && (
            <DashboardPage
              snapshot={snapshot}
              events={events}
              onNavigate={setActivePage}
            />
          )}
          {activePage === 'explorer' && (
            <ExplorerPage snapshot={snapshot} onRefresh={fetchSnapshot} notify={notify} />
          )}
          {activePage === 'sql' && (
            <SqlConsolePage snapshot={snapshot} onRefresh={fetchSnapshot} notify={notify} />
          )}
          {activePage === 'transactions' && (
            <TransactionsPage snapshot={snapshot} onRefresh={fetchSnapshot} notify={notify} />
          )}
          {activePage === 'locks' && <LocksPage snapshot={snapshot} />}
          {activePage === 'deadlocks' && <DeadlocksPage snapshot={snapshot} />}
          {activePage === 'topology' && <TopologyPage snapshot={snapshot} />}
          {activePage === 'nodes' && (
            <NodeDetailsPage snapshot={snapshot} onRefresh={fetchSnapshot} notify={notify} />
          )}
          {activePage === 'twopc' && <TwoPCPage snapshot={snapshot} />}
          {activePage === 'wal' && <WalPage snapshot={snapshot} />}
          {activePage === 'recovery' && (
            <RecoveryPage snapshot={snapshot} onRefresh={fetchSnapshot} notify={notify} />
          )}
          {activePage === 'logs' && <LogsPage logs={logs} events={events} />}
          {activePage === 'metrics' && <MetricsPage snapshot={snapshot} />}
          {activePage === 'settings' && (
            <SettingsPage
              darkTheme={darkTheme}
              onToggleTheme={() => setDarkTheme((d) => !d)}
              wsConnected={wsConnected}
              onReconnectWs={connectWs}
              user={user}
              onLogout={handleLogout}
            />
          )}
        </>
      )}
    </Layout>
  );
};

export default App;
