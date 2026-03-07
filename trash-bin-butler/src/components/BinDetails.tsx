import { MapPin, TrendingUp, Clock, AlertTriangle } from 'lucide-react'
import { useState, useEffect } from 'react'
import type { Bin } from '../types/bin'
import { getStatusBgColor, getStatusColorText } from '../utils/binStatus'
import { api } from '../services/api'

interface BinDetailsProps {
  bin: Bin
}

interface BinMetrics {
  fill_rate: string
  last_updated: string
  collections_week: number
  efficiency: string
}

export default function BinDetails({ bin }: BinDetailsProps) {
  const [metrics, setMetrics] = useState<BinMetrics | null>(null)

  const isOffline = bin.id === 0 || bin.status === 'Offline'

  useEffect(() => {
    if (isOffline) {
      setMetrics(null)
      return
    }

    api.fetchDeviceDetails(bin.id)
      .then(setMetrics)
      .catch(() => {
        setMetrics(null)
      })
  }, [bin.id, isOffline])

  return (
    <div className="space-y-6">
      <div>
        <h3 className="text-sm font-semibold text-slate-400 uppercase tracking-wide mb-4">Detailed Information</h3>

        {/* Circular Progress Indicator */}
        <div className="flex items-start gap-6">
          <div className="relative flex-shrink-0">
            <svg width="140" height="140" viewBox="0 0 140 140">
              {/* Background circle */}
              <circle
                cx="70"
                cy="70"
                r="60"
                fill="none"
                stroke="#f1f5f9"
                strokeWidth="12"
              />
              {/* Progress circle */}
              <circle
                cx="70"
                cy="70"
                r="60"
                fill="none"
                stroke="#f59e0b"
                strokeWidth="12"
                strokeDasharray={`${(bin.fill / 100) * 377} 377`}
                strokeLinecap="round"
                transform="rotate(-90 70 70)"
              />
            </svg>
            <div className="absolute inset-0 flex flex-col items-center justify-center">
              <div className="text-4xl font-extrabold text-slate-900">{bin.fill}%</div>
              <div className="text-xs text-slate-400 font-semibold uppercase tracking-wide">Filled</div>
            </div>
          </div>

          {/* Details Grid */}
          <div className="flex-1 space-y-4">
            <div className="flex items-start gap-3 p-3 bg-slate-50 rounded-lg border border-slate-200">
              <MapPin size={20} className="text-green-600 mt-0.5 flex-shrink-0" strokeWidth={2.5} />
              <div className="flex-1 min-w-0">
                <p className="text-xs text-slate-400 font-semibold uppercase tracking-wide mb-1">Location</p>
                <p className="text-sm font-bold text-slate-900">{bin.location}</p>
              </div>
            </div>

            <div className="flex items-start gap-3 p-3 bg-slate-50 rounded-lg border border-slate-200">
              <AlertTriangle size={20} className="text-yellow-500 mt-0.5 flex-shrink-0" strokeWidth={2.5} />
              <div className="flex-1 min-w-0">
                <p className="text-xs text-slate-400 font-semibold uppercase tracking-wide mb-1">Status</p>
                <div
                  className="inline-block px-3 py-1 rounded-md text-xs font-extrabold"
                  style={{
                    backgroundColor: bin.status === 'Offline' ? '#f1f5f9' : getStatusBgColor(bin.fill),
                    color: bin.status === 'Offline' ? '#64748b' : getStatusColorText(bin.fill)
                  }}
                >
                  {bin.status}
                </div>
              </div>
            </div>

            <div className="flex items-start gap-3 p-3 bg-slate-50 rounded-lg border border-slate-200">
              <Clock size={20} className="text-blue-600 mt-0.5 flex-shrink-0" strokeWidth={2.5} />
              <div className="flex-1 min-w-0">
                <p className="text-xs text-slate-400 font-semibold uppercase tracking-wide mb-1">Last Updated</p>
                {isOffline ? (
                  <p className="text-sm font-bold text-slate-400">--</p>
                ) : metrics ? (
                  <p className="text-sm font-bold text-slate-900">{metrics.last_updated}</p>
                ) : (
                  <div className="h-4 w-24 bg-slate-200 rounded animate-pulse" />
                )}
              </div>
            </div>

            <div className="flex items-start gap-3 p-3 bg-slate-50 rounded-lg border border-slate-200">
              <TrendingUp size={20} className="text-emerald-600 mt-0.5 flex-shrink-0" strokeWidth={2.5} />
              <div className="flex-1 min-w-0">
                <p className="text-xs text-slate-400 font-semibold uppercase tracking-wide mb-1">Fill Rate</p>
                {isOffline ? (
                  <p className="text-sm font-bold text-slate-400">--</p>
                ) : metrics ? (
                  <p className="text-sm font-bold text-slate-900">{metrics.fill_rate} <span className="text-slate-400 font-normal">in last 24h</span></p>
                ) : (
                  <div className="h-4 w-24 bg-slate-200 rounded animate-pulse" />
                )}
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Quick Stats */}
      <div className="grid grid-cols-3 gap-3">
        <div className="bg-gradient-to-br from-green-50 to-green-100 p-4 rounded-xl border border-green-200">
          <p className="text-xs text-green-700 font-semibold uppercase tracking-wide mb-1">Capacity</p>
          <p className="text-2xl font-extrabold text-green-900">{Math.max(0, 100 - bin.fill)}%</p>
          <p className="text-xs text-green-600 mt-1">Remaining</p>
        </div>
        <div className="bg-gradient-to-br from-blue-50 to-blue-100 p-4 rounded-xl border border-blue-200">
          <p className="text-xs text-blue-700 font-semibold uppercase tracking-wide mb-1">Collections</p>
          {isOffline ? (
            <p className="text-2xl font-extrabold text-blue-400">--</p>
          ) : metrics ? (
            <p className="text-2xl font-extrabold text-blue-900">{metrics.collections_week}</p>
          ) : (
            <div className="h-8 w-12 bg-blue-200 rounded animate-pulse my-1" />
          )}
          <p className="text-xs text-blue-600 mt-1">This week</p>
        </div>
        <div className="bg-gradient-to-br from-purple-50 to-purple-100 p-4 rounded-xl border border-purple-200">
          <p className="text-xs text-purple-700 font-semibold uppercase tracking-wide mb-1">Efficiency</p>
          {isOffline ? (
            <p className="text-2xl font-extrabold text-purple-400">--</p>
          ) : metrics ? (
            <p className="text-2xl font-extrabold text-purple-900">{metrics.efficiency}</p>
          ) : (
            <div className="h-8 w-12 bg-purple-200 rounded animate-pulse my-1" />
          )}
          <p className="text-xs text-purple-600 mt-1">Avg. score</p>
        </div>
      </div>
    </div>
  )
}
