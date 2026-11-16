import React, { useState } from 'react';
import {
  Activity,
  BarChart3,
  Cpu,
  Database,
  FileText,
  GitCommit,
  HardDrive,
  LayoutDashboard,
  Lock,
  LogOut,
  Menu,
  Moon,
  Network,
  RefreshCw,
  RotateCcw,
  ScrollText,
  Server,
  Settings,
  Share2,
  Sun,
  Terminal,
  Wifi,
  WifiOff,
  X,
} from 'lucide-react';
import type { AuthUser, ClusterSnapshot, PageId } from '../types/dlm';

export interface ToastItem {
  id: number;
  kind: 'success' | 'error' | 'info';
  message: string;
}

interface LayoutProps {
  activePage: PageId;
  onNavigate: (page: PageId) => void;
  snapshot: ClusterSnapshot | null;
  wsConnected: boolean;
  darkTheme: boolean;
  onToggleTheme: () => void;
  onRefresh: () => void;
  user: AuthUser;
  onLogout: () => void;
  toasts: ToastItem[];
  onDismissToast: (id: number) => void;
  children: React.ReactNode;
}

const NAV_ITEMS: Array<{ id: PageId; label: string; icon: React.FC<{ className?: string }> }> = [
  { id: 'overview', label: 'Dashboard Overview', icon: LayoutDashboard },
  { id: 'explorer', label: 'Database Explorer', icon: Database },
  { id: 'sql', label: 'SQL Command Console', icon: Terminal },
  { id: 'transactions', label: 'Transaction Monitor', icon: Activity },
  { id: 'locks', label: 'Lock Manager', icon: Lock },
  { id: 'deadlocks', label: 'Deadlock Visualizer', icon: Share2 },
  { id: 'topology', label: 'Cluster Topology', icon: Network },
  { id: 'nodes', label: 'Node Details', icon: Server },
  { id: 'twopc', label: '2PC Monitor', icon: GitCommit },
  { id: 'wal', label: 'WAL Viewer', icon: HardDrive },
  { id: 'recovery', label: 'Recovery Monitor', icon: RotateCcw },
  { id: 'logs', label: 'Logs', icon: ScrollText },
  { id: 'metrics', label: 'Metrics', icon: BarChart3 },
  { id: 'settings', label: 'Settings', icon: Settings },
];

export const Layout: React.FC<LayoutProps> = ({
  activePage,
  onNavigate,
  snapshot,
  wsConnected,
  darkTheme,
  onToggleTheme,
  onRefresh,
  user,
  onLogout,
  toasts,
  onDismissToast,
  children,
}) => {
  const [mobileOpen, setMobileOpen] = useState(false);

  const healthy = snapshot?.healthyNodes ?? 0;
  const total = snapshot?.totalNodes ?? 3;

  return (
    <div className="min-h-screen flex bg-slate-50 dark:bg-slate-950 text-slate-900 dark:text-slate-100">
      {/* Sidebar */}
      <aside
        className={`fixed inset-y-0 left-0 z-40 w-64 transform border-r border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 transition-transform lg:static lg:translate-x-0 flex flex-col ${
          mobileOpen ? 'translate-x-0' : '-translate-x-full'
        }`}
      >
        <div className="flex items-center justify-between px-4 py-4 border-b border-slate-200 dark:border-slate-800">
          <div className="flex items-center gap-2.5">
            <div className="rounded-lg bg-indigo-600 p-2 text-white">
              <Cpu className="h-5 w-5" />
            </div>
            <div>
              <div className="text-sm font-bold tracking-tight">DLM-DB</div>
              <div className="text-[11px] text-slate-500 dark:text-slate-400">
                Distributed KV Engine
              </div>
            </div>
          </div>
          <button
            type="button"
            onClick={() => setMobileOpen(false)}
            className="lg:hidden p-1 rounded hover:bg-slate-100 dark:hover:bg-slate-800"
            aria-label="Close navigation"
          >
            <X className="h-5 w-5" />
          </button>
        </div>

        <nav className="flex-1 overflow-y-auto px-3 py-3 space-y-1">
          {NAV_ITEMS.map((item) => {
            const Icon = item.icon;
            const active = activePage === item.id;
            return (
              <button
                key={item.id}
                type="button"
                onClick={() => {
                  onNavigate(item.id);
                  setMobileOpen(false);
                }}
                className={`w-full flex items-center gap-2.5 rounded-lg px-3 py-2 text-sm font-medium transition ${
                  active
                    ? 'bg-indigo-600 text-white shadow-sm'
                    : 'text-slate-600 dark:text-slate-300 hover:bg-slate-100 dark:hover:bg-slate-800'
                }`}
              >
                <Icon className="h-4 w-4 shrink-0" />
                <span className="truncate">{item.label}</span>
              </button>
            );
          })}
        </nav>

        <div className="border-t border-slate-200 dark:border-slate-800 p-3 text-xs space-y-2">
          <div className="flex items-center justify-between text-slate-500 dark:text-slate-400">
            <span className="flex items-center gap-1.5">
              <FileText className="h-3.5 w-3.5" />
              Signed in as <strong className="text-slate-800 dark:text-slate-200">{user.username}</strong>
            </span>
          </div>
          <button
            type="button"
            onClick={onLogout}
            className="w-full flex items-center justify-center gap-1.5 rounded-lg border border-slate-300 dark:border-slate-700 py-1.5 text-xs font-medium hover:bg-slate-100 dark:hover:bg-slate-800"
          >
            <LogOut className="h-3.5 w-3.5" />
            Sign Out
          </button>
        </div>
      </aside>

      {/* Main content */}
      <div className="flex-1 flex flex-col min-w-0">
        <header className="sticky top-0 z-30 flex items-center justify-between gap-3 border-b border-slate-200 dark:border-slate-800 bg-white/90 dark:bg-slate-900/90 backdrop-blur px-4 py-3">
          <div className="flex items-center gap-3">
            <button
              type="button"
              onClick={() => setMobileOpen(true)}
              className="lg:hidden p-1.5 rounded-lg border border-slate-300 dark:border-slate-700"
              aria-label="Open navigation"
            >
              <Menu className="h-5 w-5" />
            </button>
            <h1 className="text-base font-semibold truncate">
              {NAV_ITEMS.find((n) => n.id === activePage)?.label}
            </h1>
          </div>

          <div className="flex items-center gap-2.5 flex-wrap">
            <span
              className={`inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-medium ${
                healthy === total
                  ? 'bg-emerald-500/15 text-emerald-600 dark:text-emerald-400'
                  : healthy > 0
                  ? 'bg-amber-500/15 text-amber-600 dark:text-amber-400'
                  : 'bg-red-500/15 text-red-600 dark:text-red-400'
              }`}
            >
              <span className="h-2 w-2 rounded-full bg-current" />
              Nodes {healthy}/{total}
            </span>

            <span
              className={`inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-medium ${
                wsConnected
                  ? 'bg-indigo-500/15 text-indigo-600 dark:text-indigo-400'
                  : 'bg-slate-500/15 text-slate-500'
              }`}
              title="WebSocket Real-Time Event Stream"
            >
              {wsConnected ? <Wifi className="h-3.5 w-3.5" /> : <WifiOff className="h-3.5 w-3.5" />}
              {wsConnected ? 'WS Live' : 'WS Reconnecting'}
            </span>

            <button
              type="button"
              onClick={onRefresh}
              className="inline-flex items-center gap-1 rounded-lg border border-slate-300 dark:border-slate-700 px-2.5 py-1.5 text-xs font-medium hover:bg-slate-100 dark:hover:bg-slate-800"
              title="Refresh cluster snapshot"
            >
              <RefreshCw className="h-3.5 w-3.5" />
              Sync
            </button>

            <button
              type="button"
              onClick={onToggleTheme}
              className="p-1.5 rounded-lg border border-slate-300 dark:border-slate-700 hover:bg-slate-100 dark:hover:bg-slate-800"
              aria-label="Toggle dark or light theme"
            >
              {darkTheme ? <Sun className="h-4 w-4 text-amber-400" /> : <Moon className="h-4 w-4" />}
            </button>
          </div>
        </header>

        <main className="flex-1 p-4 md:p-6 overflow-x-hidden">{children}</main>
      </div>

      {/* Toast stack */}
      <div className="fixed bottom-4 right-4 z-50 flex flex-col gap-2 max-w-sm">
        {toasts.map((t) => (
          <div
            key={t.id}
            className={`flex items-start justify-between gap-3 rounded-lg border px-4 py-3 text-xs shadow-lg ${
              t.kind === 'error'
                ? 'border-red-500/40 bg-red-950/90 text-red-200'
                : t.kind === 'success'
                ? 'border-emerald-500/40 bg-emerald-950/90 text-emerald-200'
                : 'border-slate-700 bg-slate-900 text-slate-200'
            }`}
          >
            <span>{t.message}</span>
            <button
              type="button"
              onClick={() => onDismissToast(t.id)}
              className="opacity-70 hover:opacity-100"
              aria-label="Dismiss notification"
            >
              <X className="h-3.5 w-3.5" />
            </button>
          </div>
        ))}
      </div>
    </div>
  );
};
