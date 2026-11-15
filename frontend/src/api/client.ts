import type { AuthUser, ClusterSnapshot, SystemLog } from '../types/dlm';

const TOKEN_STORAGE_KEY = 'dlm_auth_token';

export function getStoredToken(): string | null {
  return sessionStorage.getItem(TOKEN_STORAGE_KEY);
}

export function setStoredToken(token: string | null): void {
  if (token) {
    sessionStorage.setItem(TOKEN_STORAGE_KEY, token);
  } else {
    sessionStorage.removeItem(TOKEN_STORAGE_KEY);
  }
}

export class ApiError extends Error {
  status: number;
  code: string;
  constructor(status: number, code: string, message: string) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const token = getStoredToken();
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    ...((init.headers as Record<string, string>) || {}),
  };
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

  const res = await fetch(path, { ...init, headers });
  const text = await res.text();
  let data: Record<string, unknown> = {};
  try {
    data = text ? (JSON.parse(text) as Record<string, unknown>) : {};
  } catch {
    throw new ApiError(res.status, 'PARSE_ERROR', `Invalid JSON response (${res.status})`);
  }

  if (!res.ok) {
    const errObj = data.error as { code?: string; message?: string } | undefined;
    const msg =
      errObj?.message ||
      (typeof data.error === 'string' ? data.error : '') ||
      `Request failed (${res.status})`;
    const code = errObj?.code || 'HTTP_ERROR';
    throw new ApiError(res.status, code, msg);
  }

  return data as T;
}

export const api = {
  authStatus: () => request<{ ok: boolean; setupComplete: boolean }>('/api/auth/status'),

  authSetup: (username: string, password: string) =>
    request<{ ok: boolean; token: string; user: AuthUser }>('/api/auth/setup', {
      method: 'POST',
      body: JSON.stringify({ username, password }),
    }),

  authLogin: (username: string, password: string) =>
    request<{ ok: boolean; token: string; user: AuthUser }>('/api/auth/login', {
      method: 'POST',
      body: JSON.stringify({ username, password }),
    }),

  authLogout: () => request<{ ok: boolean }>('/api/auth/logout', { method: 'POST' }),

  authMe: () => request<{ ok: boolean; user: AuthUser }>('/api/auth/me'),

  getClusterSnapshot: () => request<ClusterSnapshot>('/api/cluster'),

  crashNode: (nodeId: number) =>
    request<{ ok: boolean }>(`/api/nodes/${nodeId}/crash`, { method: 'POST' }),

  restartNode: (nodeId: number) =>
    request<{ ok: boolean }>(`/api/nodes/${nodeId}/restart`, { method: 'POST' }),

  checkpointNode: (nodeId: number) =>
    request<{ ok: boolean }>('/api/database/checkpoint', {
      method: 'POST',
      body: JSON.stringify({ nodeId }),
    }),

  getKeyOwner: (key: string) =>
    request<{ ok: boolean; key: string; ownerNode: number }>(
      `/api/database/keys/${encodeURIComponent(key)}/owner`,
    ),

  beginTxn: (nodeId: number, isolation: 'SERIALIZABLE' | 'READ_COMMITTED') =>
    request<{ ok: boolean; txnId: number; coordinatorNode: number; state: string }>(
      '/api/transactions',
      {
        method: 'POST',
        body: JSON.stringify({ nodeId, isolation }),
      },
    ),

  txnRead: (txnId: number, key: string) =>
    request<{ ok: boolean; found: boolean; value: string; ownerNode: number; status: string; error?: string }>(
      `/api/transactions/${txnId}/read`,
      {
        method: 'POST',
        body: JSON.stringify({ key }),
      },
    ),

  txnWrite: (txnId: number, key: string, value: string) =>
    request<{ ok: boolean; ownerNode: number; status: string; error?: string }>(
      `/api/transactions/${txnId}/write`,
      {
        method: 'POST',
        body: JSON.stringify({ key, value }),
      },
    ),

  txnDelete: (txnId: number, key: string) =>
    request<{ ok: boolean; ownerNode: number; status: string; error?: string }>(
      `/api/transactions/${txnId}/delete`,
      {
        method: 'POST',
        body: JSON.stringify({ key }),
      },
    ),

  txnCommit: (txnId: number) =>
    request<{ ok: boolean; txnId: number; state: string }>(
      `/api/transactions/${txnId}/commit`,
      { method: 'POST' },
    ),

  txnAbort: (txnId: number, reason = 'User requested abort') =>
    request<{ ok: boolean; txnId: number; state: string }>(
      `/api/transactions/${txnId}/abort`,
      {
        method: 'POST',
        body: JSON.stringify({ reason }),
      },
    ),

  executeSql: (command: string, nodeId: number, activeTxnId = 0) =>
    request<{
      ok: boolean;
      activeTxnId?: number;
      results?: Array<{ command: string; key?: string; ownerNode?: number; result: Record<string, unknown> }>;
      error?: string;
      line?: number;
      column?: number;
    }>('/api/sql', {
      method: 'POST',
      body: JSON.stringify({ command, nodeId, activeTxnId }),
    }),

  getLogs: () => request<{ ok: boolean; logs: SystemLog[] }>('/api/logs'),
};

export function getWebSocketUrl(token: string): string {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${window.location.host}/ws?token=${encodeURIComponent(token)}`;
}
