import React, { useState } from 'react';
import { PlayCircle, CheckCircle2, XCircle } from 'lucide-react';
import { api, ApiError } from '../api/client';
import { ConfirmDialog } from '../components/ConfirmDialog';
import type { ClusterSnapshot, TransactionInfo } from '../types/dlm';

interface TransactionsPageProps {
  snapshot: ClusterSnapshot;
  onRefresh: () => void;
  notify: (kind: 'success' | 'error' | 'info', msg: string) => void;
}

export const TransactionsPage: React.FC<TransactionsPageProps> = ({
  snapshot,
  onRefresh,
  notify,
}) => {
  const [coordNode, setCoordNode] = useState(1);
  const [isolation, setIsolation] = useState<'SERIALIZABLE' | 'READ_COMMITTED'>('SERIALIZABLE');
  const [selectedTxnId, setSelectedTxnId] = useState<number>(0);
  const [opKey, setOpKey] = useState('account_101');
  const [opValue, setOpValue] = useState('{"balance": 5000}');
  const [opResult, setOpResult] = useState('');
  const [abortTarget, setAbortTarget] = useState<number | null>(null);
  const [detailTxn, setDetailTxn] = useState<TransactionInfo | null>(null);

  const activeTxns = snapshot.transactions.filter(
    (t) => t.state === 'ACTIVE' || t.state === 'WAITING',
  );

  const handleBegin = async () => {
    try {
      const r = await api.beginTxn(coordNode, isolation);
      setSelectedTxnId(r.txnId);
      notify('success', `Started Transaction ${r.txnId} on Node ${r.coordinatorNode}`);
      onRefresh();
    } catch (err) {
      notify('error', err instanceof ApiError ? err.message : 'Failed to begin transaction');
    }
  };

  const handleOp = async (kind: 'read' | 'write' | 'delete') => {
    const tid = selectedTxnId || activeTxns[0]?.id;
    if (!tid) return notify('error', 'Begin or select an ACTIVE transaction first.');
    try {
      const r =
        kind === 'read'
          ? await api.txnRead(tid, opKey)
          : kind === 'write'
          ? await api.txnWrite(tid, opKey, opValue)
          : await api.txnDelete(tid, opKey);
      setOpResult(JSON.stringify(r, null, 2));
      notify('success', `${kind.toUpperCase()} executed on Txn ${tid}`);
      onRefresh();
    } catch (err) {
      const msg = err instanceof ApiError ? err.message : 'Operation failed';
      setOpResult(msg);
      notify('error', msg);
      onRefresh();
    }
  };

  const handleCommit = async (tid: number) => {
    try {
      const r = await api.txnCommit(tid);
      notify(r.state === 'COMMITTED' ? 'success' : 'error', `Txn ${tid}: ${r.state}`);
      onRefresh();
    } catch (err) {
      notify('error', err instanceof ApiError ? err.message : 'Commit failed');
      onRefresh();
    }
  };

  const handleAbortConfirm = async () => {
    if (!abortTarget) return;
    const tid = abortTarget;
    setAbortTarget(null);
    try {
      await api.txnAbort(tid, 'Aborted from Transaction Monitor');
      notify('info', `Txn ${tid} aborted`);
      onRefresh();
    } catch (err) {
      notify('error', err instanceof ApiError ? err.message : 'Abort failed');
    }
  };

  return (
    <div className="space-y-6">
      {/* Interactive Transaction Controller */}
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 space-y-4">
        <h2 className="text-sm font-semibold">Interactive Transaction Controller</h2>
        <div className="flex flex-wrap items-center gap-3">
          <select
            value={coordNode}
            onChange={(e) => setCoordNode(Number(e.target.value))}
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs"
          >
            {snapshot.nodes.map((n) => (
              <option key={n.nodeId} value={n.nodeId}>
                Coord: Node {n.nodeId}
              </option>
            ))}
          </select>
          <select
            value={isolation}
            onChange={(e) => setIsolation(e.target.value as 'SERIALIZABLE' | 'READ_COMMITTED')}
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs"
          >
            <option value="SERIALIZABLE">SERIALIZABLE (Strict 2PL)</option>
            <option value="READ_COMMITTED">READ_COMMITTED</option>
          </select>
          <button
            type="button"
            onClick={handleBegin}
            className="inline-flex items-center gap-1.5 rounded-lg bg-indigo-600 hover:bg-indigo-500 px-3 py-1.5 text-xs font-semibold text-white"
          >
            <PlayCircle className="h-3.5 w-3.5" />
            BEGIN Transaction
          </button>

          <select
            value={selectedTxnId || activeTxns[0]?.id || ''}
            onChange={(e) => setSelectedTxnId(Number(e.target.value))}
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs font-mono"
          >
            <option value="">Select Active Txn...</option>
            {activeTxns.map((t) => (
              <option key={t.id} value={t.id}>
                Txn {t.id} (Node {t.coordinatorNode})
              </option>
            ))}
          </select>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
          <input
            type="text"
            value={opKey}
            onChange={(e) => setOpKey(e.target.value)}
            placeholder="Key"
            className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs font-mono"
          />
          <input
            type="text"
            value={opValue}
            onChange={(e) => setOpValue(e.target.value)}
            placeholder="Value (for WRITE)"
            className="md:col-span-2 rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-3 py-1.5 text-xs font-mono"
          />
        </div>

        <div className="flex flex-wrap gap-2">
          <button
            type="button"
            onClick={() => handleOp('read')}
            className="rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-1.5 text-xs font-medium hover:bg-slate-100 dark:hover:bg-slate-800"
          >
            READ (Shared Lock)
          </button>
          <button
            type="button"
            onClick={() => handleOp('write')}
            className="rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-1.5 text-xs font-medium hover:bg-slate-100 dark:hover:bg-slate-800"
          >
            WRITE (Exclusive Lock)
          </button>
          <button
            type="button"
            onClick={() => handleOp('delete')}
            className="rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-1.5 text-xs font-medium hover:bg-slate-100 dark:hover:bg-slate-800"
          >
            DELETE (Exclusive Lock)
          </button>
        </div>

        {opResult && (
          <pre className="rounded bg-slate-50 dark:bg-slate-950 border border-slate-200 dark:border-slate-800 p-2.5 text-xs font-mono overflow-x-auto">
            {opResult}
          </pre>
        )}
      </div>

      {/* Transaction Table */}
      <div className="grid grid-cols-1 xl:grid-cols-3 gap-4">
        <div className="xl:col-span-2 rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 overflow-x-auto">
          <h3 className="text-sm font-semibold mb-3">Cluster Transactions</h3>
          {snapshot.transactions.length === 0 ? (
            <p className="text-xs text-slate-500 py-8 text-center">No transactions yet.</p>
          ) : (
            <table className="w-full text-left text-xs">
              <thead>
                <tr className="border-b border-slate-200 dark:border-slate-800 text-slate-500">
                  <th className="py-2 pr-3">Txn ID</th>
                  <th className="py-2 pr-3">State</th>
                  <th className="py-2 pr-3">Participants</th>
                  <th className="py-2 pr-3">Locks</th>
                  <th className="py-2 text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-200 dark:divide-slate-800">
                {snapshot.transactions.map((t) => (
                  <tr
                    key={t.id}
                    onClick={() => setDetailTxn(t)}
                    className="cursor-pointer hover:bg-slate-50 dark:hover:bg-slate-950/60"
                  >
                    <td className="py-2.5 pr-3 font-mono font-semibold">{t.id}</td>
                    <td className="py-2.5 pr-3">
                      <span
                        className={`rounded px-2 py-0.5 font-semibold ${
                          t.state === 'COMMITTED'
                            ? 'bg-emerald-500/15 text-emerald-500'
                            : t.state === 'ABORTED'
                            ? 'bg-red-500/15 text-red-500'
                            : 'bg-indigo-500/15 text-indigo-500'
                        }`}
                      >
                        {t.state}
                      </span>
                    </td>
                    <td className="py-2.5 pr-3">[{t.participants.join(', ')}]</td>
                    <td className="py-2.5 pr-3">{t.lockedResources.length}</td>
                    <td className="py-2.5 text-right space-x-1" onClick={(e) => e.stopPropagation()}>
                      {t.state === 'ACTIVE' && (
                        <>
                          <button
                            type="button"
                            onClick={() => handleCommit(t.id)}
                            className="inline-flex items-center gap-1 rounded bg-emerald-600/20 text-emerald-500 px-2 py-1 hover:bg-emerald-600/30"
                          >
                            <CheckCircle2 className="h-3 w-3" />
                            Commit
                          </button>
                          <button
                            type="button"
                            onClick={() => setAbortTarget(t.id)}
                            className="inline-flex items-center gap-1 rounded bg-red-600/20 text-red-500 px-2 py-1 hover:bg-red-600/30"
                          >
                            <XCircle className="h-3 w-3" />
                            Abort
                          </button>
                        </>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>

        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 text-xs space-y-3">
          <h3 className="text-sm font-semibold">Transaction Detail</h3>
          {detailTxn ? (
            <div className="space-y-2">
              <div>
                <strong>Txn ID:</strong> <span className="font-mono">{detailTxn.id}</span>
              </div>
              <div>
                <strong>Coordinator:</strong> Node {detailTxn.coordinatorNode} · {detailTxn.isolation}
              </div>
              <div>
                <strong>State:</strong> {detailTxn.state}
              </div>
              {detailTxn.abortReason && (
                <div className="text-red-400">
                  <strong>Reason:</strong> {detailTxn.abortReason}
                </div>
              )}
              <div>
                <strong>Locked Resources:</strong>{' '}
                {detailTxn.lockedResources.length > 0
                  ? detailTxn.lockedResources.join(', ')
                  : 'None'}
              </div>
              <div>
                <strong>Operations ({detailTxn.operations.length}):</strong>
                <pre className="mt-1 rounded bg-slate-50 dark:bg-slate-950 border border-slate-200 dark:border-slate-800 p-2 font-mono overflow-x-auto max-h-60">
                  {JSON.stringify(detailTxn.operations, null, 2)}
                </pre>
              </div>
            </div>
          ) : (
            <p className="text-slate-500 py-8 text-center">
              Click any transaction row to inspect its operations and participants.
            </p>
          )}
        </div>
      </div>

      <ConfirmDialog
        open={abortTarget !== null}
        title="Abort Transaction"
        message={`Abort Transaction ${abortTarget} and release all acquired locks?`}
        confirmLabel="Abort Transaction"
        danger
        onConfirm={handleAbortConfirm}
        onCancel={() => setAbortTarget(null)}
      />
    </div>
  );
};
