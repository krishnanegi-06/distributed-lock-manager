import React, { useState } from 'react';
import { HardDrive, Power, RotateCw, Server } from 'lucide-react';
import { api, ApiError } from '../api/client';
import { ConfirmDialog } from '../components/ConfirmDialog';
import type { ClusterSnapshot } from '../types/dlm';

interface NodeDetailsPageProps {
  snapshot: ClusterSnapshot;
  onRefresh: () => void;
  notify: (kind: 'success' | 'error' | 'info', msg: string) => void;
}

export const NodeDetailsPage: React.FC<NodeDetailsPageProps> = ({
  snapshot,
  onRefresh,
  notify,
}) => {
  const [selectedId, setSelectedId] = useState<number>(snapshot.nodes[0]?.nodeId ?? 1);
  const [crashTarget, setCrashTarget] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);

  const node = snapshot.nodes.find((n) => n.nodeId === selectedId) || snapshot.nodes[0];
  const nodeRecords = snapshot.records.filter((r) => r.ownerNode === node?.nodeId);
  const nodeLocks = snapshot.locks.filter((l) => l.nodeId === node?.nodeId);

  const handleCrash = async () => {
    if (crashTarget === null) return;
    const nid = crashTarget;
    setCrashTarget(null);
    setBusy(true);
    try {
      await api.crashNode(nid);
      notify('info', `Node ${nid} crash simulated.`);
      onRefresh();
    } catch (err) {
      notify('error', err instanceof ApiError ? err.message : 'Crash failed');
    } finally {
      setBusy(false);
    }
  };

  const handleRestart = async (nid: number) => {
    setBusy(true);
    try {
      await api.restartNode(nid);
      notify('success', `Node ${nid} restarted with WAL recovery.`);
      onRefresh();
    } catch (err) {
      notify('error', err instanceof ApiError ? err.message : 'Restart failed');
    } finally {
      setBusy(false);
    }
  };

  const handleCheckpoint = async (nid: number) => {
    setBusy(true);
    try {
      await api.checkpointNode(nid);
      notify('success', `Checkpoint & WAL compaction completed on Node ${nid}.`);
      onRefresh();
    } catch (err) {
      notify('error', err instanceof ApiError ? err.message : 'Checkpoint failed');
    } finally {
      setBusy(false);
    }
  };

  if (!node) return null;

  return (
    <div className="space-y-6">
      <div className="flex flex-wrap gap-2">
        {snapshot.nodes.map((n) => (
          <button
            key={n.nodeId}
            type="button"
            onClick={() => setSelectedId(n.nodeId)}
            className={`inline-flex items-center gap-2 rounded-lg border px-3 py-2 text-xs font-semibold ${
              node.nodeId === n.nodeId
                ? 'border-indigo-500 bg-indigo-600 text-white'
                : 'border-slate-300 dark:border-slate-700 bg-white dark:bg-slate-900'
            }`}
          >
            <Server className="h-3.5 w-3.5" />
            Node {n.nodeId} ({n.status})
          </button>
        ))}
      </div>

      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 space-y-4">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div>
            <h2 className="text-base font-bold">
              Node {node.nodeId} <span className="font-mono text-xs text-slate-500">:{node.port}</span>
            </h2>
            <p className="text-xs text-slate-500">
              Status: <strong>{node.status}</strong> · Checkpoint LSN:{' '}
              {node.storage?.checkpointLsn ?? 0}
            </p>
          </div>
          <div className="flex flex-wrap gap-2">
            <button
              type="button"
              disabled={busy || !node.reachable}
              onClick={() => handleCheckpoint(node.nodeId)}
              className="inline-flex items-center gap-1.5 rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-1.5 text-xs font-semibold disabled:opacity-40 hover:bg-slate-100 dark:hover:bg-slate-800"
            >
              <HardDrive className="h-3.5 w-3.5" />
              Checkpoint WAL
            </button>
            {node.reachable ? (
              <button
                type="button"
                disabled={busy}
                onClick={() => setCrashTarget(node.nodeId)}
                className="inline-flex items-center gap-1.5 rounded-lg bg-red-600 hover:bg-red-700 px-3 py-1.5 text-xs font-semibold text-white disabled:opacity-40"
              >
                <Power className="h-3.5 w-3.5" />
                Crash Node
              </button>
            ) : (
              <button
                type="button"
                disabled={busy}
                onClick={() => handleRestart(node.nodeId)}
                className="inline-flex items-center gap-1.5 rounded-lg bg-emerald-600 hover:bg-emerald-700 px-3 py-1.5 text-xs font-semibold text-white disabled:opacity-40"
              >
                <RotateCw className="h-3.5 w-3.5" />
                Restart & Recover
              </button>
            )}
          </div>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs">
          <div className="rounded-lg border border-slate-200 dark:border-slate-800 p-3">
            <h3 className="font-semibold mb-2">Owned Keys ({nodeRecords.length})</h3>
            {nodeRecords.length === 0 ? (
              <p className="text-slate-500">No keys stored on this shard.</p>
            ) : (
              <ul className="space-y-1 font-mono max-h-56 overflow-y-auto">
                {nodeRecords.map((r) => (
                  <li key={r.key} className="flex justify-between">
                    <span>{r.key}</span>
                    <span className="text-slate-500">v{r.version}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>

          <div className="rounded-lg border border-slate-200 dark:border-slate-800 p-3">
            <h3 className="font-semibold mb-2">Local Locks ({nodeLocks.length})</h3>
            {nodeLocks.length === 0 ? (
              <p className="text-slate-500">No active locks on this node.</p>
            ) : (
              <ul className="space-y-1 font-mono max-h-56 overflow-y-auto">
                {nodeLocks.map((l) => (
                  <li key={l.resourceId} className="flex justify-between">
                    <span>{l.resourceId}</span>
                    <span>
                      {l.mode} (owners: {l.owners.join(',')})
                    </span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>
      </div>

      <ConfirmDialog
        open={crashTarget !== null}
        title="Simulate Node Crash"
        message={`Terminate/disconnect Node ${crashTarget}? Shards owned by Node ${crashTarget} will report as unavailable until restarted.`}
        confirmLabel="Crash Node"
        danger
        onConfirm={handleCrash}
        onCancel={() => setCrashTarget(null)}
      />
    </div>
  );
};
