import { useMemo } from 'react'
import { Trash2, Settings } from 'lucide-react'
import type { Bin, BinSummary } from '../types/bin'
import { getStatusColor } from '../utils/binStatus'

interface SidebarProps {
  bins: Bin[]
  selectedBin: Bin
  onSelectBin: (bin: Bin) => void
  onSettings?: () => void
}

export default function Sidebar({ bins, selectedBin, onSelectBin, onSettings }: SidebarProps) {
  const summary = useMemo<BinSummary>(() => {
    return bins.reduce(
      (acc, bin) => {
        const status = bin.status === 'Offline' ? 'Normal' : (bin.status === 'Critical' ? 'Critical' : (bin.status === 'Warning' ? 'Warning' : 'Normal'))
        acc[status] = (acc[status] || 0) + 1
        return acc
      },
      { Normal: 0, Warning: 0, Critical: 0 }
    )
  }, [bins])

  return (
    <aside className="w-72 bg-white border-r border-slate-200 flex flex-col shadow-sm">
      {/* Header */}
      <div className="p-6 border-b border-slate-200">
        <div className="flex items-center gap-3 mb-4">
          <div className="p-2 bg-green-50 rounded-lg">
            <Trash2 size={24} className="text-green-600" strokeWidth={2.5} />
          </div>
          <div>
            <h1 className="text-lg font-bold text-slate-900">Waste Bins</h1>
            <p className="text-xs text-slate-500 font-medium">{bins.length} monitored</p>
          </div>
        </div>

        {/* System Health Badges - Compact at Top */}
        <div className="flex gap-2">
          <div className="flex-1 bg-green-50 rounded-lg px-2 py-1.5 border border-green-200">
            <div className="flex items-center justify-center gap-1">
              <div className="w-1.5 h-1.5 rounded-full bg-green-500" />
              <span className="text-xs font-bold text-green-700">{summary.Normal}</span>
            </div>
          </div>
          <div className="flex-1 bg-yellow-50 rounded-lg px-2 py-1.5 border border-yellow-200">
            <div className="flex items-center justify-center gap-1">
              <div className="w-1.5 h-1.5 rounded-full bg-yellow-500" />
              <span className="text-xs font-bold text-yellow-700">{summary.Warning}</span>
            </div>
          </div>
          <div className="flex-1 bg-red-50 rounded-lg px-2 py-1.5 border border-red-200">
            <div className="flex items-center justify-center gap-1">
              <div className="w-1.5 h-1.5 rounded-full bg-red-500" />
              <span className="text-xs font-bold text-red-700">{summary.Critical}</span>
            </div>
          </div>
        </div>
      </div>

      {/* Bin List */}
      <div className="flex-1 overflow-y-auto p-4">
        <h2 className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-3 px-2">Active Bins</h2>
        <div className="space-y-2">
          {bins.map(bin => {
            const isSelected = selectedBin.id === bin.id

            return (
              <button
                key={bin.id}
                onClick={() => onSelectBin(bin)}
                className={`w-full text-left p-3 rounded-lg transition-all duration-200 relative ${isSelected
                  ? 'bg-green-50 border-l-4 border-green-600 shadow-sm pl-4'
                  : 'bg-slate-50 hover:bg-slate-100 hover:shadow-sm border-l-4 border-transparent'
                  }`}
              >
                <div className="flex items-center justify-between mb-2">
                  <div className="flex items-center gap-2">
                    <div
                      className={`w-2 h-2 rounded-full ${isSelected ? 'ring-2 ring-green-200' : ''}`}
                      style={{ backgroundColor: getStatusColor(bin.fill) }}
                    />
                    <span className={`font-semibold text-sm ${isSelected ? 'text-green-900' : 'text-slate-900'}`}>
                      {bin.name}
                    </span>
                  </div>
                  <span className={`text-sm font-extrabold ${isSelected ? 'text-green-700' : 'text-slate-700'}`}>
                    {bin.fill}%
                  </span>
                </div>
                <div className="h-1.5 bg-slate-200 rounded-full overflow-hidden">
                  <div
                    className="h-full rounded-full transition-all duration-300"
                    style={{
                      width: `${bin.fill}%`,
                      backgroundColor: getStatusColor(bin.fill)
                    }}
                  />
                </div>
                <p className="text-xs text-slate-500 mt-2 font-medium">{bin.location}</p>
              </button>
            )
          })}
        </div>
      </div>

      {/* Settings Footer */}
      {onSettings && (
        <div className="p-4 border-t border-slate-200">
          <button
            onClick={onSettings}
            className="w-full flex items-center gap-3 px-4 py-3 rounded-lg bg-slate-50 hover:bg-slate-100 transition-colors text-slate-700 hover:text-slate-900"
          >
            <Settings size={20} className="text-slate-500" />
            <span className="font-medium text-sm">Settings</span>
          </button>
        </div>
      )}
    </aside>
  )
}
