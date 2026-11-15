import React, { useState } from 'react';
import { Lock } from 'lucide-react';
import type { ClusterSnapshot } from '../types/dlm';

export const LocksPage: React.FC<{ snapshot: ClusterSnapshot }> = ({ snapshot }) => {
  const [filter, setFilter] = useState('');
  const locks = snapshot.locks.filter((l) =>
    filter ? l.resourceId.toLowerCase().includes(filter.toLowerCase()) : true,
  );

  return (
    <div className="space-y-4">
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <div className="flex flex-wrap items-center justify-between gap-3 mb-4">
          <h2 className="text-sm font-semibold flex items-center gap-2">
            <Lock className="h-4 w-4 text-blue-500" />
            Cluster Lock Table ({locks.length})
          </h2>
          <input
            type="text"
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
            placeholder="Filter by resource/key..."
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs"
          />
        </div>

        {locks.length === 0 ? (
          <p className="text-xs text-slate-500 py-8 text-center">
            No active or waiting locks across the cluster.
          </p>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs">
              <thead>
                <tr className="border-b border-slate-200 dark:border-slate-800 text-slate-500">
                  <th className="py-2 pr-3">Resource / Key</th>
                  <th className="py-2 pr-3">Node</th>
                  <th className="py-2 pr-3">Mode</th>
                  <th className="py-2 pr-3">Owners</th>
                  <th className="py-2 pr-3">Wait Queue</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-200 dark:divide-slate-800">
                {locks.map((l) => (
                  <tr key={`${l.nodeId}-${l.resourceId}`}>
                    <td className="py-2.5 pr-3 font-mono font-semibold">{l.resourceId}</td>
                    <td className="py-2.5 pr-3">Node {l.nodeId}</td>
                    <td className="py-2.5 pr-3">
                      <span
                        className={`rounded px-2 py-0.5 font-semibold ${
                          l.mode === 'EXCLUSIVE'
                            ? 'bg-orange-500/15 text-orange-500'
                            : 'bg-blue-500/15 text-blue-500'
                        }`}
                      >
                        {l.mode}
                      </span>
                    </td>
                    <td className="py-2.5 pr-3 font-mono">{l.owners.join(', ') || 'None'}</td>
                    <td className="py-2.5 pr-3 font-mono">
                      {l.waiters.length > 0
                        ? l.waiters.map((w) => `${w.txnId} (${w.mode})`).join(', ')
                        : 'Empty'}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
};
