import React, { useState } from 'react';
import { Database, Lock, ShieldCheck, UserPlus } from 'lucide-react';
import { api, ApiError, setStoredToken } from '../api/client';
import type { AuthUser } from '../types/dlm';

interface AuthPageProps {
  setupComplete: boolean;
  onAuthenticated: (token: string, user: AuthUser) => void;
}

export const AuthPage: React.FC<AuthPageProps> = ({ setupComplete, onAuthenticated }) => {
  const [username, setUsername] = useState('admin');
  const [password, setPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const isSetup = !setupComplete;

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    if (isSetup && password !== confirmPassword) {
      setError('Passwords do not match.');
      return;
    }
    setLoading(true);
    try {
      const resp = isSetup
        ? await api.authSetup(username.trim(), password)
        : await api.authLogin(username.trim(), password);
      setStoredToken(resp.token);
      onAuthenticated(resp.token, resp.user);
    } catch (err) {
      if (err instanceof ApiError) {
        setError(err.message);
      } else {
        setError('Unable to reach backend API server.');
      }
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen flex items-center justify-center bg-slate-950 px-4">
      <div className="w-full max-w-md rounded-2xl border border-slate-800 bg-slate-900/90 p-8 shadow-2xl">
        <div className="flex items-center gap-3 mb-6">
          <div className="rounded-xl bg-indigo-600/20 p-3 text-indigo-400">
            <Database className="h-7 w-7" />
          </div>
          <div>
            <h1 className="text-xl font-bold text-white">DLM-DB Console</h1>
            <p className="text-xs text-slate-400">
              Distributed Transactional Key-Value Database
            </p>
          </div>
        </div>

        <div className="mb-6 rounded-lg border border-slate-800 bg-slate-950/60 p-3.5 text-xs text-slate-300 flex items-start gap-2.5">
          {isSetup ? (
            <UserPlus className="h-4 w-4 text-amber-400 shrink-0 mt-0.5" />
          ) : (
            <ShieldCheck className="h-4 w-4 text-emerald-400 shrink-0 mt-0.5" />
          )}
          <div>
            {isSetup ? (
              <span>
                <strong>First-Run Admin Setup:</strong> Create the initial administrator
                account. Credentials are salted and hashed with PBKDF2-HMAC-SHA256 on the
                C++ backend.
              </span>
            ) : (
              <span>
                Sign in with your administrator credentials to access the cluster API and
                real-time WebSocket stream.
              </span>
            )}
          </div>
        </div>

        <form onSubmit={handleSubmit} className="space-y-4">
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1">
              Username
            </label>
            <input
              type="text"
              required
              minLength={3}
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              className="w-full rounded-lg border border-slate-700 bg-slate-950 px-3 py-2 text-sm text-white focus:border-indigo-500 focus:outline-none"
            />
          </div>

          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1">
              Password
            </label>
            <input
              type="password"
              required
              minLength={8}
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="Minimum 8 characters"
              className="w-full rounded-lg border border-slate-700 bg-slate-950 px-3 py-2 text-sm text-white focus:border-indigo-500 focus:outline-none"
            />
          </div>

          {isSetup && (
            <div>
              <label className="block text-xs font-medium text-slate-300 mb-1">
                Confirm Password
              </label>
              <input
                type="password"
                required
                minLength={8}
                value={confirmPassword}
                onChange={(e) => setConfirmPassword(e.target.value)}
                className="w-full rounded-lg border border-slate-700 bg-slate-950 px-3 py-2 text-sm text-white focus:border-indigo-500 focus:outline-none"
              />
            </div>
          )}

          {error && (
            <div className="rounded-lg border border-red-500/30 bg-red-500/10 px-3 py-2 text-xs text-red-400">
              {error}
            </div>
          )}

          <button
            type="submit"
            disabled={loading}
            className="w-full flex items-center justify-center gap-2 rounded-lg bg-indigo-600 hover:bg-indigo-500 disabled:opacity-50 px-4 py-2.5 text-sm font-semibold text-white transition"
          >
            <Lock className="h-4 w-4" />
            {loading
              ? 'Authenticating...'
              : isSetup
              ? 'Create Admin Account'
              : 'Sign In'}
          </button>
        </form>
      </div>
    </div>
  );
};
