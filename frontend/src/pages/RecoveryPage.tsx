import React from 'react';
import { HardDrive, RotateCcw } from 'lucide-react';
import { api, ApiError } from '../api/client';
import type { ClusterSnapshot } from '../types/dlm';

interface RecoveryPageProps {
  snapshot: ClusterSnapshot;
  onRefresh: () => void;
  notify: (kind: 'success' | 'error' | 'info', msg: string) => void;
}

export const RecoveryPage: React.FC<RecoveryPageProps> = ({
  snapshot,
  onRefresh,
  notify,
}) => {
  const triggerCheckpoint = async (nodeId: number) => {
    try {
      await api.checkpointNode(nodeId);
      notify('success', `Node ${nodeId} checkpointed and WAL compacted.`);
      onRefresh();
    } catch (err) {
      notify('error', err instanceof ApiError ? err.message : 'Checkpoint failed');
    }
  };

  return (
    <div className="space-y-4">
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <h2 className="text-sm font-semibold mb-4 flex items-center gap-2">
          <RotateCcw className="h-4 w-4 text-emerald-500" />
          Startup Crash Recovery & Checkpoint Status
        </h2>
        {snapshot.recovery.length === 0 ? (
          <p className="text-xs text-slate-500 py-8 text-center">
            No reachable node recovery reports available.
          </p>
        ) : (
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            {snapshot.recovery.map((r) => (
              <div
                key={r.nodeId}
                className="rounded-xl border border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-950 p-4 text-xs space-y-2"
              >
                <div className="flex items-center justify-between">
                  <span className="font-bold text-sm">Node {r.nodeId}</span>
                  <span className="rounded bg-emerald-500/15 text-emerald-500 px-2 py-0.5 font-semibold">
                    {r.executed ? 'RECOVERED' : 'IDLE'}
                  </span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-500">Checkpoint LSN</span>
                  <span className="font-mono">{r.checkpointLsn}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-500">WAL Records Scanned</span>
                  <span className="font-mono">{r.walRecordsScanned}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-500">Redone Transactions</span>
                  <span className="font-mono text-emerald-500">{r.redoneTransactions}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-500">Undone Transactions</span>
                  <span className="font-mono text-amber-500">{r.undoneTransactions}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-500">In-Doubt (2PC Prepared)</span>
                  <span className="font-mono">{r.inDoubtTransactions}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-500">Truncated Corrupt Tail</span>
                  <span className="font-mono">{r.truncatedCorruptTail ? 'YES' : 'NO'}</span>
                </div>
                <button
                  type="button"
                  onClick={() => triggerCheckpoint(r.nodeId)}
                  className="mt-2 w-full inline-flex items-center justify-center gap-1.5 rounded-lg border border-slate-300 dark:border-slate-700 py-1.5 font-semibold hover:bg-slate-200/50 dark:hover:bg-slate-800"
                >
                  <HardDrive className="h-3.5 w-3.5" />
                  Create Checkpoint
                </button>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
};
