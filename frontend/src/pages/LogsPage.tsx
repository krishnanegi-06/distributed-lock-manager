import React from 'react';
import { ScrollText } from 'lucide-react';
import type { SystemLog, WsEvent } from '../types/dlm';

interface LogsPageProps {
  logs: SystemLog[];
  events: WsEvent[];
}

export const LogsPage: React.FC<LogsPageProps> = ({ logs, events }) => {
  return (
    <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <h2 className="text-sm font-semibold mb-3 flex items-center gap-2">
          <ScrollText className="h-4 w-4 text-indigo-500" />
          Backend System Logs ({logs.length})
        </h2>
        <div className="space-y-1.5 max-h-[500px] overflow-y-auto font-mono text-xs">
          {logs.length === 0 ? (
            <p className="text-slate-500 py-8 text-center">No system logs recorded yet.</p>
          ) : (
            logs
              .slice()
              .reverse()
              .map((l, idx) => (
                <div
                  key={idx}
                  className="rounded border border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-950 px-3 py-1.5"
                >
                  <span className="text-slate-500">
                    {new Date(l.timestamp).toLocaleTimeString()}
                  </span>{' '}
                  <span
                    className={`font-semibold ${
                      l.level === 'ERROR'
                        ? 'text-red-500'
                        : l.level === 'WARN'
                        ? 'text-amber-500'
                        : 'text-emerald-500'
                    }`}
                  >
                    [{l.level}]
                  </span>{' '}
                  <span className="text-indigo-500">[Node {l.nodeId}]</span> {l.message}
                </div>
              ))
          )}
        </div>
      </div>

      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <h2 className="text-sm font-semibold mb-3">Real-Time WebSocket Events ({events.length})</h2>
        <div className="space-y-1.5 max-h-[500px] overflow-y-auto font-mono text-xs">
          {events.length === 0 ? (
            <p className="text-slate-500 py-8 text-center">Awaiting WebSocket events...</p>
          ) : (
            events.map((e) => (
              <div
                key={e.eventId}
                className="rounded border border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-950 px-3 py-1.5"
              >
                <span className="text-slate-500">#{e.eventId}</span>{' '}
                <span className="text-indigo-500 font-semibold">{e.type}</span>{' '}
                <span className="text-slate-500">node={e.nodeId}</span>{' '}
                <span>{JSON.stringify(e.payload)}</span>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
};
