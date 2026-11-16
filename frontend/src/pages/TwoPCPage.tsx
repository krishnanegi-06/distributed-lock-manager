import React from 'react';
import { GitCommit } from 'lucide-react';
import type { ClusterSnapshot } from '../types/dlm';

export const TwoPCPage: React.FC<{ snapshot: ClusterSnapshot }> = ({ snapshot }) => {
  return (
    <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
      <h2 className="text-sm font-semibold mb-4 flex items-center gap-2">
        <GitCommit className="h-4 w-4 text-purple-500" />
        Two-Phase Commit (2PC) Coordinator & Participant Log
      </h2>
      {snapshot.twoPc.length === 0 ? (
        <p className="text-xs text-slate-500 py-8 text-center">
          No distributed 2PC transactions executed yet. Run a transaction touching keys on 2+
          nodes.
        </p>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs">
            <thead>
              <tr className="border-b border-slate-200 dark:border-slate-800 text-slate-500">
                <th className="py-2 pr-3">Txn ID</th>
                <th className="py-2 pr-3">Role</th>
                <th className="py-2 pr-3">State</th>
                <th className="py-2 pr-3">Votes</th>
                <th className="py-2 pr-3">ACKs</th>
                <th className="py-2 pr-3">Durable Decision</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-200 dark:divide-slate-800">
              {snapshot.twoPc.map((tp, idx) => (
                <tr key={`${tp.txnId}-${tp.role}-${idx}`}>
                  <td className="py-2.5 pr-3 font-mono font-semibold">{tp.txnId}</td>
                  <td className="py-2.5 pr-3">{tp.role}</td>
                  <td className="py-2.5 pr-3 font-semibold">{tp.state}</td>
                  <td className="py-2.5 pr-3 font-mono">{JSON.stringify(tp.votes)}</td>
                  <td className="py-2.5 pr-3 font-mono">{JSON.stringify(tp.acks)}</td>
                  <td className="py-2.5 pr-3">
                    {tp.decisionDurable ? (
                      <span className="text-emerald-500 font-semibold">YES (fsynced)</span>
                    ) : (
                      <span className="text-slate-500">N/A</span>
                    )}
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
