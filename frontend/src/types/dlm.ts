export interface PeerHealth {
  nodeId: number;
  state: 'HEALTHY' | 'SUSPECTED' | 'FAILED' | 'SHUTDOWN';
  lastHeartbeatEpochMs: number;
  missedBeats: number;
}

export interface StorageStats {
  keyCount: number;
  checkpointLsn: number;
  lastSyncEpochMs: number;
  recoveredCleanly: boolean;
}

export interface NodeInfo {
  nodeId: number;
  port: number;
  status: 'HEALTHY' | 'SUSPECTED' | 'FAILED' | 'SHUTDOWN';
  reachable: boolean;
  keyCount: number;
  activeLocks: number;
  activeTxns: number;
  storage?: StorageStats;
  peers?: PeerHealth[];
}

export interface KeyRecord {
  key: string;
  value: string;
  isJson: boolean;
  version: number;
  updatedAtMs: number;
  ownerNode: number;
}

export interface LockWaiter {
  txnId: number;
  mode: 'SHARED' | 'EXCLUSIVE';
}

export interface LockInfo {
  resourceId: string;
  mode: 'SHARED' | 'EXCLUSIVE';
  owners: number[];
  waiters: LockWaiter[];
  nodeId: number;
}

export interface WaitEdge {
  waitingTxn: number;
  holdingTxn: number;
  resourceId: string;
  nodeId: number;
}

export interface TxnOperation {
  type: number;
  resourceId: string;
  lockMode: 'SHARED' | 'EXCLUSIVE';
  data: string;
  ownerNode: number;
}

export interface TransactionInfo {
  id: number;
  coordinatorNode: number;
  state: 'ACTIVE' | 'WAITING' | 'PREPARING' | 'PREPARED' | 'COMMITTED' | 'ABORTED';
  isolation: 'SERIALIZABLE' | 'READ_COMMITTED';
  startEpochMs: number;
  endEpochMs: number;
  abortReason: string;
  lockedResources: string[];
  participants: number[];
  operations: TxnOperation[];
}

export interface TwoPCInfo {
  txnId: number;
  role: 'COORDINATOR' | 'PARTICIPANT';
  state: string;
  coordinatorNode: number;
  participants: number[];
  votes: Record<string, boolean>;
  acks: Record<string, boolean>;
  decisionDurable: boolean;
  updatedAtMs: number;
}

export interface DeadlockInfo {
  cycle: number[];
  victim: number;
  detectedAtMs: number;
  resolved: boolean;
}

export interface WalEntry {
  lsn: number;
  type: string;
  txnId: number;
  nodeId: number;
  resourceId: string;
  data: string;
  prevData: string;
  prevExisted: boolean;
  participants: number[];
  timestamp: number;
}

export interface RecoveryReport {
  nodeId: number;
  executed: boolean;
  truncatedCorruptTail: boolean;
  checkpointLsn: number;
  walRecordsScanned: number;
  redoneTransactions: number;
  undoneTransactions: number;
  inDoubtTransactions: number;
  inDoubtTxnIds: number[];
  recoveredAtMs: number;
}

export interface MetricsInfo {
  uptimeMs: number;
  httpRequests: number;
  httpErrors: number;
  txnsStarted: number;
  txnsCommitted: number;
  txnsAborted: number;
  avgTxnLatencyMs: number;
  throughputTxnsPerSec: number;
}

export interface ClusterSnapshot {
  timestamp: number;
  totalNodes: number;
  healthyNodes: number;
  failedNodes: number;
  nodes: NodeInfo[];
  records: KeyRecord[];
  locks: LockInfo[];
  waitEdges: WaitEdge[];
  transactions: TransactionInfo[];
  twoPc: TwoPCInfo[];
  deadlocks: DeadlockInfo[];
  deadlockCount: number;
  wal: WalEntry[];
  recovery: RecoveryReport[];
  metrics: MetricsInfo;
}

export interface WsEvent {
  eventId: number;
  timestamp: number;
  type: string;
  nodeId: number;
  txnId?: number;
  payload: Record<string, unknown>;
}

export interface SystemLog {
  timestamp: number;
  level: string;
  nodeId: number;
  message: string;
}

export interface AuthUser {
  username: string;
  role: string;
  expiresAtMs?: number;
}

export type PageId =
  | 'overview'
  | 'explorer'
  | 'sql'
  | 'transactions'
  | 'locks'
  | 'deadlocks'
  | 'topology'
  | 'nodes'
  | 'twopc'
  | 'wal'
  | 'recovery'
  | 'logs'
  | 'metrics'
  | 'settings';
