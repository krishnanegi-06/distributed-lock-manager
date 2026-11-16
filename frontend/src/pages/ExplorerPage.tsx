import React, { useMemo, useState } from 'react';
import { Plus, Search, Trash2, Edit3, Eye } from 'lucide-react';
import { api, ApiError } from '../api/client';
import { ConfirmDialog } from '../components/ConfirmDialog';
import type { ClusterSnapshot, KeyRecord } from '../types/dlm';

interface ExplorerPageProps {
  snapshot: ClusterSnapshot;
  onRefresh: () => void;
  notify: (kind: 'success' | 'error' | 'info', msg: string) => void;
}

export const ExplorerPage: React.FC<ExplorerPageProps> = ({
  snapshot,
  onRefresh,
  notify,
}) => {
  const [search, setSearch] = useState('');
  const [nodeFilter, setNodeFilter] = useState<string>('ALL');
  const [selectedRecord, setSelectedRecord] = useState<KeyRecord | null>(null);

  const [newKey, setNewKey] = useState('');
  const [newValue, setNewValue] = useState('{"balance": 5000}');
  const [busy, setBusy] = useState(false);
  const [errorBanner, setErrorBanner] = useState('');
  const [deleteTarget, setDeleteTarget] = useState<string | null>(null);

  const filtered = useMemo(() => {
    return snapshot.records.filter((r) => {
      if (search && !r.key.toLowerCase().includes(search.toLowerCase())) return false;
      if (nodeFilter !== 'ALL' && String(r.ownerNode) !== nodeFilter) return false;
      return true;
    });
  }, [snapshot.records, search, nodeFilter]);

  const handlePut = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!newKey.trim()) return;
    setBusy(true);
    setErrorBanner('');
    try {
      const ownerInfo = await api.getKeyOwner(newKey.trim());
      const beg = await api.beginTxn(ownerInfo.ownerNode, 'SERIALIZABLE');
      const wr = await api.txnWrite(beg.txnId, newKey.trim(), newValue);
      if (!wr.ok) {
        await api.txnAbort(beg.txnId, wr.error || 'Write failed');
        throw new Error(wr.error || wr.status);
      }
      await api.txnCommit(beg.txnId);
      notify('success', `Committed key "${newKey.trim()}" on Node ${ownerInfo.ownerNode}`);
      setNewKey('');
      onRefresh();
    } catch (err) {
      const msg = err instanceof ApiError ? err.message : (err as Error).message;
      setErrorBanner(msg);
      notify('error', msg);
    } finally {
      setBusy(false);
    }
  };

  const handleDeleteConfirm = async () => {
    if (!deleteTarget) return;
    const key = deleteTarget;
    setDeleteTarget(null);
    setBusy(true);
    setErrorBanner('');
    try {
      const ownerInfo = await api.getKeyOwner(key);
      const beg = await api.beginTxn(ownerInfo.ownerNode, 'SERIALIZABLE');
      const del = await api.txnDelete(beg.txnId, key);
      if (!del.ok) {
        await api.txnAbort(beg.txnId, del.error || 'Delete failed');
        throw new Error(del.error || del.status);
      }
      await api.txnCommit(beg.txnId);
      notify('success', `Deleted key "${key}" from Node ${ownerInfo.ownerNode}`);
      if (selectedRecord?.key === key) setSelectedRecord(null);
      onRefresh();
    } catch (err) {
      const msg = err instanceof ApiError ? err.message : (err as Error).message;
      setErrorBanner(msg);
      notify('error', msg);
    } finally {
      setBusy(false);
    }
  };

  const formatValue = (val: string, isJson: boolean) => {
    if (!isJson) return val;
    try {
      return JSON.stringify(JSON.parse(val), null, 2);
    } catch {
      return val;
    }
  };

  return (
    <div className="space-y-6">
      {errorBanner && (
        <div className="rounded-lg border border-red-500/40 bg-red-500/10 px-4 py-3 text-xs text-red-400">
          Backend Error: {errorBanner}
        </div>
      )}

      {/* Insert / Update Record Form */}
      <form
        onSubmit={handlePut}
        className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 space-y-4"
      >
        <h2 className="text-sm font-semibold flex items-center gap-2">
          <Plus className="h-4 w-4 text-indigo-500" />
          Insert / Update Record (Transactional PUT)
        </h2>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
          <input
            type="text"
            required
            placeholder="Key (e.g. account_101)"
            value={newKey}
            onChange={(e) => setNewKey(e.target.value)}
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-2 text-sm font-mono focus:border-indigo-500 focus:outline-none"
          />
          <input
            type="text"
            required
            placeholder='JSON or String (e.g. {"balance": 5000})'
            value={newValue}
            onChange={(e) => setNewValue(e.target.value)}
            className="md:col-span-2 rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-2 text-sm font-mono focus:border-indigo-500 focus:outline-none"
          />
        </div>
        <button
          type="submit"
          disabled={busy}
          className="rounded-lg bg-indigo-600 hover:bg-indigo-500 disabled:opacity-50 px-4 py-2 text-xs font-semibold text-white"
        >
          {busy ? 'Executing Transaction...' : 'Write & Commit'}
        </button>
      </form>

      {/* Filter & Records Table */}
      <div className="grid grid-cols-1 xl:grid-cols-3 gap-4">
        <div className="xl:col-span-2 rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
          <div className="flex flex-wrap items-center justify-between gap-3 mb-4">
            <div className="relative flex-1 min-w-[200px]">
              <Search className="h-4 w-4 absolute left-3 top-2.5 text-slate-400" />
              <input
                type="text"
                value={search}
                onChange={(e) => setSearch(e.target.value)}
                placeholder="Filter keys..."
                className="w-full rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 pl-9 pr-3 py-1.5 text-sm focus:border-indigo-500 focus:outline-none"
              />
            </div>
            <select
              value={nodeFilter}
              onChange={(e) => setNodeFilter(e.target.value)}
              className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-sm"
            >
              <option value="ALL">All Shard Nodes</option>
              {snapshot.nodes.map((n) => (
                <option key={n.nodeId} value={String(n.nodeId)}>
                  Node {n.nodeId}
                </option>
              ))}
            </select>
          </div>

          {filtered.length === 0 ? (
            <div className="py-12 text-center text-sm text-slate-500">
              No committed keys match your filter.
            </div>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-left text-xs">
                <thead>
                  <tr className="border-b border-slate-200 dark:border-slate-800 text-slate-500">
                    <th className="py-2 pr-3">Key</th>
                    <th className="py-2 pr-3">Shard Owner</th>
                    <th className="py-2 pr-3">Type</th>
                    <th className="py-2 pr-3">Version</th>
                    <th className="py-2 text-right">Actions</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-200 dark:divide-slate-800">
                  {filtered.map((rec) => (
                    <tr key={rec.key} className="hover:bg-slate-50 dark:hover:bg-slate-950/60">
                      <td className="py-2.5 pr-3 font-mono font-semibold">{rec.key}</td>
                      <td className="py-2.5 pr-3">
                        <span className="rounded bg-indigo-500/15 text-indigo-500 px-2 py-0.5 font-semibold">
                          Node {rec.ownerNode}
                        </span>
                      </td>
                      <td className="py-2.5 pr-3">{rec.isJson ? 'JSON' : 'STRING'}</td>
                      <td className="py-2.5 pr-3">v{rec.version}</td>
                      <td className="py-2.5 text-right space-x-2">
                        <button
                          type="button"
                          onClick={() => setSelectedRecord(rec)}
                          className="inline-flex items-center gap-1 rounded border border-slate-300 dark:border-slate-700 px-2 py-1 hover:bg-slate-100 dark:hover:bg-slate-800"
                        >
                          <Eye className="h-3 w-3" />
                          View
                        </button>
                        <button
                          type="button"
                          onClick={() => {
                            setNewKey(rec.key);
                            setNewValue(rec.value);
                          }}
                          className="inline-flex items-center gap-1 rounded border border-slate-300 dark:border-slate-700 px-2 py-1 hover:bg-slate-100 dark:hover:bg-slate-800"
                        >
                          <Edit3 className="h-3 w-3" />
                          Edit
                        </button>
                        <button
                          type="button"
                          onClick={() => setDeleteTarget(rec.key)}
                          className="inline-flex items-center gap-1 rounded border border-red-500/30 text-red-500 px-2 py-1 hover:bg-red-500/10"
                        >
                          <Trash2 className="h-3 w-3" />
                          Delete
                        </button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>

        {/* Value Inspector */}
        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
          <h3 className="text-sm font-semibold mb-3">Record Inspector</h3>
          {selectedRecord ? (
            <div className="space-y-3 text-xs">
              <div>
                <span className="text-slate-500">Key:</span>{' '}
                <span className="font-mono font-bold">{selectedRecord.key}</span>
              </div>
              <div>
                <span className="text-slate-500">Owner:</span> Node {selectedRecord.ownerNode} · v
                {selectedRecord.version}
              </div>
              <pre className="rounded-lg border border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-950 p-3 font-mono text-xs overflow-x-auto max-h-80">
                {formatValue(selectedRecord.value, selectedRecord.isJson)}
              </pre>
            </div>
          ) : (
            <p className="text-xs text-slate-500 py-8 text-center">
              Select a key from the table to inspect its committed value.
            </p>
          )}
        </div>
      </div>

      <ConfirmDialog
        open={deleteTarget !== null}
        title="Delete Key"
        message={`Execute a transactional DELETE and COMMIT for key "${deleteTarget}"?`}
        confirmLabel="Delete Key"
        danger
        onConfirm={handleDeleteConfirm}
        onCancel={() => setDeleteTarget(null)}
      />
    </div>
  );
};
