import React from 'react';
import { Activity, AlertTriangle, Database, Lock, Server, Zap } from 'lucide-react';
import type { ClusterSnapshot, PageId, WsEvent } from '../types/dlm';

interface DashboardPageProps {
  snapshot: ClusterSnapshot;
  events: WsEvent[];
  onNavigate: (page: PageId) => void;
}

export const DashboardPage: React.FC<DashboardPageProps> = ({
  snapshot,
  events,
  onNavigate,
}) => {
  const activeTxns = snapshot.transactions.filter(
    (t) => t.state === 'ACTIVE' || t.state === 'PREPARING' || t.state === 'PREPARED',
  ).length;
  const waitingTxns = snapshot.transactions.filter((t) => t.state === 'WAITING').length;
  const committedTxns = snapshot.transactions.filter((t) => t.state === 'COMMITTED').length;
  const abortedTxns = snapshot.transactions.filter((t) => t.state === 'ABORTED').length;

  const kpis = [
    {
      label: 'Total / Healthy Nodes',
      value: `${snapshot.healthyNodes} / ${snapshot.totalNodes}`,
      sub: `${snapshot.failedNodes} failed/unreachable`,
      icon: Server,
      color: snapshot.failedNodes > 0 ? 'text-amber-500' : 'text-emerald-500',
    },
    {
      label: 'Active / Waiting Txns',
      value: `${activeTxns} / ${waitingTxns}`,
      sub: `${snapshot.transactions.length} total tracked`,
      icon: Activity,
      color: 'text-indigo-500',
    },
    {
      label: 'Active Locks',
      value: `${snapshot.locks.length}`,
      sub: `${snapshot.waitEdges.length} wait edges`,
      icon: Lock,
      color: 'text-blue-500',
    },
    {
      label: 'Deadlocks Detected',
      value: `${snapshot.deadlockCount}`,
      sub: `${snapshot.deadlocks.length} in history`,
      icon: AlertTriangle,
      color: snapshot.deadlockCount > 0 ? 'text-red-500' : 'text-slate-400',
    },
    {
      label: 'Committed / Aborted',
      value: `${Math.max(committedTxns, snapshot.metrics.txnsCommitted)} / ${Math.max(abortedTxns, snapshot.metrics.txnsAborted)}`,
      sub: `Throughput ${snapshot.metrics.throughputTxnsPerSec.toFixed(2)} txn/s`,
      icon: Zap,
      color: 'text-emerald-500',
    },
    {
      label: 'Avg Txn Latency',
      value: `${snapshot.metrics.avgTxnLatencyMs.toFixed(1)} ms`,
      sub: `${snapshot.records.length} committed keys`,
      icon: Database,
      color: 'text-purple-500',
    },
  ];

  return (
    <div className="space-y-6">
      {/* KPI Cards */}
      <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-3 gap-4">
        {kpis.map((k) => {
          const Icon = k.icon;
          return (
            <div
              key={k.label}
              className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-4 shadow-sm"
            >
              <div className="flex items-center justify-between">
                <span className="text-xs font-medium text-slate-500 dark:text-slate-400">
                  {k.label}
                </span>
                <Icon className={`h-4 w-4 ${k.color}`} />
              </div>
              <div className="mt-2 text-2xl font-bold tracking-tight">{k.value}</div>
              <div className="mt-1 text-xs text-slate-500 dark:text-slate-400">{k.sub}</div>
            </div>
          );
        })}
      </div>

      {/* Node Health Strip */}
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-sm font-semibold">Cluster Nodes</h2>
          <button
            type="button"
            onClick={() => onNavigate('nodes')}
            className="text-xs text-indigo-500 hover:underline font-medium"
          >
            Manage Nodes →
          </button>
        </div>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
          {snapshot.nodes.map((node) => (
            <div
              key={node.nodeId}
              className="rounded-lg border border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-950 p-4"
            >
              <div className="flex items-center justify-between">
                <span className="font-semibold text-sm">
                  Node {node.nodeId} <span className="font-mono text-xs text-slate-500">:{node.port}</span>
                </span>
                <span
                  className={`rounded-full px-2 py-0.5 text-[11px] font-semibold ${
                    node.reachable
                      ? 'bg-emerald-500/15 text-emerald-500'
                      : 'bg-red-500/15 text-red-500'
                  }`}
                >
                  {node.status}
                </span>
              </div>
              <div className="mt-3 grid grid-cols-3 gap-2 text-xs">
                <div>
                  <div className="text-slate-500">Keys</div>
                  <div className="font-semibold">{node.keyCount}</div>
                </div>
                <div>
                  <div className="text-slate-500">Locks</div>
                  <div className="font-semibold">{node.activeLocks}</div>
                </div>
                <div>
                  <div className="text-slate-500">Active Txns</div>
                  <div className="font-semibold">{node.activeTxns}</div>
                </div>
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Recent WebSocket Events & Recent Transactions */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
          <div className="flex items-center justify-between mb-3">
            <h2 className="text-sm font-semibold">Recent Backend Events (WebSocket)</h2>
            <button
              type="button"
              onClick={() => onNavigate('logs')}
              className="text-xs text-indigo-500 hover:underline font-medium"
            >
              View All →
            </button>
          </div>
          {events.length === 0 ? (
            <p className="text-xs text-slate-500 py-6 text-center">
              No WebSocket events captured yet. Execute a transaction or SQL command.
            </p>
          ) : (
            <div className="space-y-2 max-h-64 overflow-y-auto font-mono text-xs">
              {events.slice(0, 15).map((ev) => (
                <div
                  key={ev.eventId}
                  className="flex items-center justify-between rounded bg-slate-50 dark:bg-slate-950 px-3 py-1.5 border border-slate-200 dark:border-slate-800"
                >
                  <span className="font-semibold text-indigo-500">{ev.type}</span>
                  <span className="text-slate-500">
                    Node {ev.nodeId} {ev.txnId ? `· Txn ${ev.txnId}` : ''}
                  </span>
                </div>
              ))}
            </div>
          )}
        </div>

        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
          <div className="flex items-center justify-between mb-3">
            <h2 className="text-sm font-semibold">Latest Transactions</h2>
            <button
              type="button"
              onClick={() => onNavigate('transactions')}
              className="text-xs text-indigo-500 hover:underline font-medium"
            >
              Open Monitor →
            </button>
          </div>
          {snapshot.transactions.length === 0 ? (
            <p className="text-xs text-slate-500 py-6 text-center">
              No transactions recorded yet.
            </p>
          ) : (
            <div className="space-y-2 max-h-64 overflow-y-auto text-xs">
              {snapshot.transactions.slice(0, 10).map((t) => (
                <div
                  key={t.id}
                  className="flex items-center justify-between rounded bg-slate-50 dark:bg-slate-950 px-3 py-2 border border-slate-200 dark:border-slate-800"
                >
                  <div>
                    <span className="font-mono font-semibold">Txn {t.id}</span>
                    <span className="ml-2 text-slate-500">
                      Coord: Node {t.coordinatorNode} · Parts: [{t.participants.join(', ')}]
                    </span>
                  </div>
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
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
