import React, { useMemo, useState } from 'react';
import { HardDrive } from 'lucide-react';
import type { ClusterSnapshot } from '../types/dlm';

export const WalPage: React.FC<{ snapshot: ClusterSnapshot }> = ({ snapshot }) => {
  const [nodeFilter, setNodeFilter] = useState('ALL');
  const [txnFilter, setTxnFilter] = useState('');

  const filtered = useMemo(() => {
    return snapshot.wal
      .filter((w) => {
        if (nodeFilter !== 'ALL' && String(w.nodeId) !== nodeFilter) return false;
        if (txnFilter && !String(w.txnId).includes(txnFilter.trim())) return false;
        return true;
      })
      .slice()
      .reverse();
  }, [snapshot.wal, nodeFilter, txnFilter]);

  return (
    <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <h2 className="text-sm font-semibold flex items-center gap-2">
          <HardDrive className="h-4 w-4 text-indigo-500" />
          Write-Ahead Log (WAL) Records ({filtered.length})
        </h2>
        <div className="flex flex-wrap gap-2">
          <select
            value={nodeFilter}
            onChange={(e) => setNodeFilter(e.target.value)}
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs"
          >
            <option value="ALL">All Nodes</option>
            {snapshot.nodes.map((n) => (
              <option key={n.nodeId} value={String(n.nodeId)}>
                Node {n.nodeId}
              </option>
            ))}
          </select>
          <input
            type="text"
            value={txnFilter}
            onChange={(e) => setTxnFilter(e.target.value)}
            placeholder="Filter Txn ID..."
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs font-mono"
          />
        </div>
      </div>

      {filtered.length === 0 ? (
        <p className="text-xs text-slate-500 py-8 text-center">No WAL records match filter.</p>
      ) : (
        <div className="overflow-x-auto max-h-[540px]">
          <table className="w-full text-left text-xs">
            <thead>
              <tr className="border-b border-slate-200 dark:border-slate-800 text-slate-500">
                <th className="py-2 pr-3">LSN</th>
                <th className="py-2 pr-3">Node</th>
                <th className="py-2 pr-3">Type</th>
                <th className="py-2 pr-3">Txn ID</th>
                <th className="py-2 pr-3">Key / Resource</th>
                <th className="py-2 pr-3">Payload / Undo</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-200 dark:divide-slate-800 font-mono">
              {filtered.map((w, idx) => (
                <tr key={`${w.nodeId}-${w.lsn}-${idx}`}>
                  <td className="py-2 pr-3">{w.lsn}</td>
                  <td className="py-2 pr-3">Node {w.nodeId}</td>
                  <td className="py-2 pr-3 font-semibold text-indigo-500">{w.type}</td>
                  <td className="py-2 pr-3">{w.txnId}</td>
                  <td className="py-2 pr-3">{w.resourceId || '-'}</td>
                  <td className="py-2 pr-3 truncate max-w-xs" title={w.data}>
                    {w.data || '-'}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
};
