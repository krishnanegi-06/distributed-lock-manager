import React from 'react';
import { RefreshCw, Settings, Sun, Moon, LogOut } from 'lucide-react';
import type { AuthUser } from '../types/dlm';

interface SettingsPageProps {
  darkTheme: boolean;
  onToggleTheme: () => void;
  wsConnected: boolean;
  onReconnectWs: () => void;
  user: AuthUser;
  onLogout: () => void;
}

export const SettingsPage: React.FC<SettingsPageProps> = ({
  darkTheme,
  onToggleTheme,
  wsConnected,
  onReconnectWs,
  user,
  onLogout,
}) => {
  return (
    <div className="max-w-2xl space-y-6">
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 space-y-4">
        <h2 className="text-sm font-semibold flex items-center gap-2">
          <Settings className="h-4 w-4 text-indigo-500" />
          Interface & Connection Settings
        </h2>

        <div className="flex items-center justify-between border-t border-slate-200 dark:border-slate-800 pt-4 text-xs">
          <div>
            <div className="font-semibold">Color Theme</div>
            <div className="text-slate-500">Switch between Dark and Light dashboard mode</div>
          </div>
          <button
            type="button"
            onClick={onToggleTheme}
            className="inline-flex items-center gap-2 rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-1.5 font-semibold"
          >
            {darkTheme ? <Sun className="h-4 w-4 text-amber-400" /> : <Moon className="h-4 w-4" />}
            {darkTheme ? 'Switch to Light' : 'Switch to Dark'}
          </button>
        </div>

        <div className="flex items-center justify-between border-t border-slate-200 dark:border-slate-800 pt-4 text-xs">
          <div>
            <div className="font-semibold">WebSocket Stream</div>
            <div className="text-slate-500">
              Current state: <strong>{wsConnected ? 'Connected' : 'Disconnected'}</strong>
            </div>
          </div>
          <button
            type="button"
            onClick={onReconnectWs}
            className="inline-flex items-center gap-2 rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-1.5 font-semibold"
          >
            <RefreshCw className="h-3.5 w-3.5" />
            Reconnect WS & Sync
          </button>
        </div>

        <div className="flex items-center justify-between border-t border-slate-200 dark:border-slate-800 pt-4 text-xs">
          <div>
            <div className="font-semibold">Authenticated Session</div>
            <div className="text-slate-500">
              User: <strong>{user.username}</strong> ({user.role})
            </div>
          </div>
          <button
            type="button"
            onClick={onLogout}
            className="inline-flex items-center gap-2 rounded-lg bg-red-600/15 text-red-500 px-3 py-1.5 font-semibold hover:bg-red-600/25"
          >
            <LogOut className="h-3.5 w-3.5" />
            Sign Out
          </button>
        </div>
      </div>
    </div>
  );
};
