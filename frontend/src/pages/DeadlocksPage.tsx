import React from 'react';
import { AlertTriangle, Share2 } from 'lucide-react';
import type { ClusterSnapshot } from '../types/dlm';

export const DeadlocksPage: React.FC<{ snapshot: ClusterSnapshot }> = ({ snapshot }) => {
  const edges = snapshot.waitEdges;
  const nodeIds = Array.from(
    new Set(edges.flatMap((e) => [e.waitingTxn, e.holdingTxn])),
  );

  const latestDeadlock = snapshot.deadlocks[snapshot.deadlocks.length - 1];
  const cycleSet = new Set(latestDeadlock?.cycle ?? []);

  const width = 620;
  const height = 280;
  const cx = width / 2;
  const cy = height / 2;
  const radius = Math.min(cx, cy) - 55;

  const positions = new Map<number, { x: number; y: number }>();
  nodeIds.forEach((id, i) => {
    const angle = (i / Math.max(1, nodeIds.length)) * 2 * Math.PI - Math.PI / 2;
    positions.set(id, {
      x: cx + radius * Math.cos(angle),
      y: cy + radius * Math.sin(angle),
    });
  });

  return (
    <div className="space-y-6">
      {/* Live Wait-For Graph */}
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-sm font-semibold flex items-center gap-2">
            <Share2 className="h-4 w-4 text-indigo-500" />
            Live Wait-For Graph (Backend WFG)
          </h2>
          <span className="text-xs text-slate-500">
            {edges.length} wait edges · {snapshot.deadlockCount} total cycles resolved
          </span>
        </div>

        {edges.length === 0 ? (
          <div className="py-16 text-center text-xs text-slate-500 border border-dashed border-slate-300 dark:border-slate-800 rounded-lg">
            No active wait-for edges across the cluster. Wait-For Graph is acyclic.
          </div>
        ) : (
          <div className="overflow-x-auto">
            <svg
              viewBox={`0 0 ${width} ${height}`}
              className="w-full max-w-3xl mx-auto rounded-lg bg-slate-50 dark:bg-slate-950 border border-slate-200 dark:border-slate-800"
            >
              <defs>
                <marker
                  id="arrow"
                  viewBox="0 0 10 10"
                  refX="18"
                  refY="5"
                  markerWidth="6"
                  markerHeight="6"
                  orient="auto-start-reverse"
                >
                  <path d="M 0 0 L 10 5 L 0 10 z" fill="#6366f1" />
                </marker>
                <marker
                  id="arrow-red"
                  viewBox="0 0 10 10"
                  refX="18"
                  refY="5"
                  markerWidth="6"
                  markerHeight="6"
                  orient="auto-start-reverse"
                >
                  <path d="M 0 0 L 10 5 L 0 10 z" fill="#ef4444" />
                </marker>
              </defs>

              {edges.map((e, idx) => {
                const from = positions.get(e.waitingTxn);
                const to = positions.get(e.holdingTxn);
                if (!from || !to) return null;
                const inCycle = cycleSet.has(e.waitingTxn) && cycleSet.has(e.holdingTxn);
                return (
                  <g key={idx}>
                    <line
                      x1={from.x}
                      y1={from.y}
                      x2={to.x}
                      y2={to.y}
                      stroke={inCycle ? '#ef4444' : '#6366f1'}
                      strokeWidth={inCycle ? 2.5 : 1.75}
                      markerEnd={inCycle ? 'url(#arrow-red)' : 'url(#arrow)'}
                    />
                    <text
                      x={(from.x + to.x) / 2}
                      y={(from.y + to.y) / 2 - 6}
                      textAnchor="middle"
                      className="fill-slate-500 text-[10px] font-mono"
                    >
                      {e.resourceId}
                    </text>
                  </g>
                );
              })}

              {nodeIds.map((id) => {
                const pos = positions.get(id)!;
                const isVictim = latestDeadlock?.victim === id;
                return (
                  <g key={id}>
                    <circle
                      cx={pos.x}
                      cy={pos.y}
                      r={20}
                      className={
                        isVictim
                          ? 'fill-red-600 stroke-red-300'
                          : 'fill-indigo-600 stroke-indigo-300'
                      }
                      strokeWidth={2}
                    />
                    <text
                      x={pos.x}
                      y={pos.y + 4}
                      textAnchor="middle"
                      className="fill-white text-[10px] font-mono font-bold"
                    >
                      {id}
                    </text>
                  </g>
                );
              })}
            </svg>
          </div>
        )}
      </div>

      {/* Deadlock Resolution History */}
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <h3 className="text-sm font-semibold mb-3 flex items-center gap-2">
          <AlertTriangle className="h-4 w-4 text-amber-500" />
          Detected & Resolved Deadlock Cycles
        </h3>
        {snapshot.deadlocks.length === 0 ? (
          <p className="text-xs text-slate-500 py-6 text-center">
            No deadlock cycles detected in current session.
          </p>
        ) : (
          <div className="space-y-2 text-xs">
            {snapshot.deadlocks.map((d, idx) => (
              <div
                key={idx}
                className="flex flex-wrap items-center justify-between gap-2 rounded-lg border border-red-500/30 bg-red-500/5 px-3 py-2"
              >
                <span className="font-mono">
                  Cycle: <strong>{d.cycle.join(' → ')}</strong>
                </span>
                <div className="flex items-center gap-2">
                  <span className="rounded bg-red-500/15 text-red-500 px-2 py-0.5 font-semibold">
                    Victim Aborted: {d.victim}
                  </span>
                  <span className="rounded bg-emerald-500/15 text-emerald-500 px-2 py-0.5 font-semibold">
                    {d.resolved ? 'RESOLVED' : 'PENDING'}
                  </span>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
};
