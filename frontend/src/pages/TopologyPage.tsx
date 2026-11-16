import React, { useState } from 'react';
import { Network, Search } from 'lucide-react';
import { api } from '../api/client';
import type { ClusterSnapshot } from '../types/dlm';

export const TopologyPage: React.FC<{ snapshot: ClusterSnapshot }> = ({ snapshot }) => {
  const [probeKey, setProbeKey] = useState('account_101');
  const [probeOwner, setProbeOwner] = useState<number | null>(null);

  const checkOwner = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!probeKey.trim()) return;
    try {
      const r = await api.getKeyOwner(probeKey.trim());
      setProbeOwner(r.ownerNode);
    } catch {
      setProbeOwner(null);
    }
  };

  return (
    <div className="space-y-6">
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <h2 className="text-sm font-semibold mb-4 flex items-center gap-2">
          <Network className="h-4 w-4 text-indigo-500" />
          Cluster Topology & Peer Communication
        </h2>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
          {snapshot.nodes.map((node) => (
            <div
              key={node.nodeId}
              className={`rounded-xl border p-4 ${
                node.reachable
                  ? 'border-emerald-500/30 bg-emerald-500/5'
                  : 'border-red-500/40 bg-red-500/5'
              }`}
            >
              <div className="flex items-center justify-between mb-2">
                <div className="font-bold text-sm">Node {node.nodeId}</div>
                <span className="font-mono text-xs text-slate-500">TCP :{node.port}</span>
              </div>
              <div className="text-xs mb-3">
                Health:{' '}
                <strong className={node.reachable ? 'text-emerald-500' : 'text-red-500'}>
                  {node.status}
                </strong>
              </div>
              <div className="text-xs text-slate-500 mb-1">Peer Heartbeat Links:</div>
              <div className="space-y-1 text-xs">
                {(node.peers || []).length === 0 ? (
                  <div className="text-slate-500 italic">No peer data (node offline)</div>
                ) : (
                  node.peers!.map((p) => (
                    <div
                      key={p.nodeId}
                      className="flex items-center justify-between rounded bg-white/60 dark:bg-slate-950/70 px-2 py-1"
                    >
                      <span>Peer Node {p.nodeId}</span>
                      <span
                        className={`font-semibold ${
                          p.state === 'HEALTHY'
                            ? 'text-emerald-500'
                            : p.state === 'SUSPECTED'
                            ? 'text-amber-500'
                            : 'text-red-500'
                        }`}
                      >
                        {p.state}
                      </span>
                    </div>
                  ))
                )}
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Shard Ownership Calculator */}
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
        <h3 className="text-sm font-semibold mb-2">FNV-1a Deterministic Shard Ownership Lookup</h3>
        <p className="text-xs text-slate-500 mb-3">
          Query the C++ backend router (`fnv1a64(key) % num_nodes`) to inspect which node owns a
          key.
        </p>
        <form onSubmit={checkOwner} className="flex flex-wrap items-center gap-3">
          <div className="relative">
            <Search className="h-3.5 w-3.5 absolute left-3 top-2.5 text-slate-400" />
            <input
              type="text"
              value={probeKey}
              onChange={(e) => setProbeKey(e.target.value)}
              className="rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 pl-8 pr-3 py-1.5 text-xs font-mono"
            />
          </div>
          <button
            type="submit"
            className="rounded-lg bg-indigo-600 hover:bg-indigo-500 px-3 py-1.5 text-xs font-semibold text-white"
          >
            Resolve Shard Owner
          </button>
          {probeOwner !== null && (
            <span className="rounded-lg bg-indigo-500/15 text-indigo-500 px-3 py-1.5 text-xs font-semibold">
              Owner: Node {probeOwner}
            </span>
          )}
        </form>
      </div>
    </div>
  );
};
