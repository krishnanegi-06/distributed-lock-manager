import React from 'react';
import { BarChart3 } from 'lucide-react';
import type { ClusterSnapshot } from '../types/dlm';

export const MetricsPage: React.FC<{ snapshot: ClusterSnapshot }> = ({ snapshot }) => {
  const m = snapshot.metrics;
  const maxKeys = Math.max(1, ...snapshot.nodes.map((n) => n.keyCount));

  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-4">
          <div className="text-xs text-slate-500">Transaction Throughput</div>
          <div className="mt-1 text-2xl font-bold">{m.throughputTxnsPerSec.toFixed(2)} txn/s</div>
        </div>
        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-4">
          <div className="text-xs text-slate-500">Average Txn Latency</div>
          <div className="mt-1 text-2xl font-bold">{m.avgTxnLatencyMs.toFixed(2)} ms</div>
        </div>
        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-4">
          <div className="text-xs text-slate-500">Committed / Aborted</div>
          <div className="mt-1 text-2xl font-bold">
            {m.txnsCommitted} / {m.txnsAborted}
          </div>
        </div>
        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-4">
          <div className="text-xs text-slate-500">API Requests / Errors</div>
          <div className="mt-1 text-2xl font-bold">
            {m.httpRequests} / {m.httpErrors}
          </div>
        </div>
      </div>

      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <h2 className="text-sm font-semibold mb-4 flex items-center gap-2">
          <BarChart3 className="h-4 w-4 text-indigo-500" />
          Shard Key Distribution Across Nodes
        </h2>
        <div className="space-y-3">
          {snapshot.nodes.map((n) => {
            const pct = Math.round((n.keyCount / maxKeys) * 100);
            return (
              <div key={n.nodeId} className="space-y-1 text-xs">
                <div className="flex justify-between">
                  <span className="font-semibold">
                    Node {n.nodeId} ({n.status})
                  </span>
                  <span className="font-mono">
                    {n.keyCount} keys · {n.activeLocks} locks · {n.activeTxns} active txns
                  </span>
                </div>
                <div className="h-2.5 w-full rounded-full bg-slate-200 dark:bg-slate-800 overflow-hidden">
                  <div
                    className="h-full rounded-full bg-indigo-600 transition-all"
                    style={{ width: `${pct}%` }}
                  />
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
};
