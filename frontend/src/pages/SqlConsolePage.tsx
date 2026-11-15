import React, { useState } from 'react';
import { Play, Terminal } from 'lucide-react';
import { api, ApiError } from '../api/client';
import type { ClusterSnapshot } from '../types/dlm';

interface SqlConsolePageProps {
  snapshot: ClusterSnapshot;
  onRefresh: () => void;
  notify: (kind: 'success' | 'error' | 'info', msg: string) => void;
}

const SAMPLE_SCRIPT = `BEGIN;
PUT account_101 {"balance":5000};
PUT account_102 {"balance":2500};
GET account_101;
COMMIT;`;

export const SqlConsolePage: React.FC<SqlConsolePageProps> = ({
  snapshot,
  onRefresh,
  notify,
}) => {
  const [script, setScript] = useState(SAMPLE_SCRIPT);
  const [nodeId, setNodeId] = useState<number>(1);
  const [activeTxnId, setActiveTxnId] = useState<number>(0);
  const [running, setRunning] = useState(false);
  const [output, setOutput] = useState<string>('');
  const [errorMsg, setErrorMsg] = useState<string>('');

  const handleRun = async () => {
    setRunning(true);
    setErrorMsg('');
    try {
      const res = await api.executeSql(script, nodeId, activeTxnId);
      setActiveTxnId(res.activeTxnId || 0);
      setOutput(JSON.stringify(res, null, 2));
      if (res.ok) {
        notify('success', 'SQL-like command batch executed via C++ TransactionManager.');
      } else {
        setErrorMsg(res.error || 'Command batch reported an error.');
      }
      onRefresh();
    } catch (err) {
      const msg = err instanceof ApiError ? err.message : (err as Error).message;
      setErrorMsg(msg);
      notify('error', msg);
    } finally {
      setRunning(false);
    }
  };

  return (
    <div className="grid grid-cols-1 xl:grid-cols-3 gap-6">
      <div className="xl:col-span-2 space-y-4">
        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 space-y-4">
          <div className="flex flex-wrap items-center justify-between gap-3">
            <h2 className="text-sm font-semibold flex items-center gap-2">
              <Terminal className="h-4 w-4 text-indigo-500" />
              SQL-like Transactional Command Editor
            </h2>
            <div className="flex items-center gap-2 text-xs">
              <span>Coordinator:</span>
              <select
                value={nodeId}
                onChange={(e) => setNodeId(Number(e.target.value))}
                className="rounded border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 px-2 py-1"
              >
                {snapshot.nodes.map((n) => (
                  <option key={n.nodeId} value={n.nodeId}>
                    Node {n.nodeId}
                  </option>
                ))}
              </select>
              {activeTxnId > 0 && (
                <span className="rounded bg-amber-500/15 text-amber-500 px-2 py-1 font-mono font-semibold">
                  Open Txn: {activeTxnId}
                </span>
              )}
            </div>
          </div>

          <textarea
            rows={9}
            value={script}
            onChange={(e) => setScript(e.target.value)}
            className="w-full rounded-lg border border-slate-300 dark:border-slate-700 bg-slate-50 dark:bg-slate-950 p-3 font-mono text-sm focus:border-indigo-500 focus:outline-none"
            placeholder="BEGIN; PUT key value; GET key; COMMIT;"
          />

          <div className="flex flex-wrap items-center gap-2">
            <button
              type="button"
              disabled={running}
              onClick={handleRun}
              className="inline-flex items-center gap-2 rounded-lg bg-indigo-600 hover:bg-indigo-500 disabled:opacity-50 px-4 py-2 text-xs font-semibold text-white"
            >
              <Play className="h-3.5 w-3.5" />
              {running ? 'Executing...' : 'Execute Script'}
            </button>
            <button
              type="button"
              onClick={() =>
                setScript(`BEGIN;\nDELETE account_101;\nABORT;`)
              }
              className="rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-2 text-xs hover:bg-slate-100 dark:hover:bg-slate-800"
            >
              Load Abort Example
            </button>
            <button
              type="button"
              onClick={() => setScript(`SCAN;\nSHOW LOCKS;\nSHOW TRANSACTIONS;`)}
              className="rounded-lg border border-slate-300 dark:border-slate-700 px-3 py-2 text-xs hover:bg-slate-100 dark:hover:bg-slate-800"
            >
              Load Inspect Example
            </button>
          </div>

          {errorMsg && (
            <div className="rounded-lg border border-red-500/40 bg-red-500/10 px-3 py-2 text-xs text-red-400">
              {errorMsg}
            </div>
          )}
        </div>

        <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5">
          <h3 className="text-sm font-semibold mb-2">Execution Result</h3>
          <pre className="rounded-lg border border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-950 p-3 font-mono text-xs overflow-x-auto max-h-96">
            {output || 'Run a command above to view backend execution results.'}
          </pre>
        </div>
      </div>

      {/* Supported Grammar Panel */}
      <div className="rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-900 p-5 space-y-3 text-xs h-fit">
        <h3 className="text-sm font-semibold">Supported Command Grammar</h3>
        <p className="text-slate-500 dark:text-slate-400">
          This is a minimal transactional key-value command language executed directly by the
          C++ transaction engine (not full ANSI SQL).
        </p>
        <pre className="rounded-lg bg-slate-50 dark:bg-slate-950 border border-slate-200 dark:border-slate-800 p-3 font-mono text-[11px] overflow-x-auto">
{`BEGIN [SERIALIZABLE | READ COMMITTED];
PUT <key> <json_or_quoted_string>;
GET <key>;
DELETE <key>;
EXISTS <key>;
SCAN [<prefix>];
COMMIT;
ABORT; | ROLLBACK;
SHOW LOCKS;
SHOW TRANSACTIONS;
SHOW NODES;`}
        </pre>
      </div>
    </div>
  );
};
