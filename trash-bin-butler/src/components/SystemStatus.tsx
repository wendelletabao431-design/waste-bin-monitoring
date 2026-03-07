import { Clock, Users, Activity } from 'lucide-react'
import { useState, useEffect } from 'react'
import { api } from '../services/api'

export default function SystemStatus() {
  const [data, setData] = useState<any>(null)

  useEffect(() => {
    api.fetchSummary().then(setData).catch(console.error)
  }, [])

  const currentDate = new Date()
  const formattedDate = currentDate.toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric'
  })
  const formattedTime = currentDate.toLocaleTimeString('en-US', {
    hour: '2-digit',
    minute: '2-digit',
    hour12: true
  })

  return (
    <div className="px-6 py-4 bg-gradient-to-r from-slate-50 to-slate-100 border-t border-slate-200 flex items-center justify-between">
      {/* Left Section */}
      <div className="flex items-center gap-6">
        {/* Last Update */}
        <div className="flex items-center gap-2">
          <div className="p-1.5 bg-blue-100 rounded-lg">
            <Clock size={14} className="text-blue-600" strokeWidth={2.5} />
          </div>
          <div>
            <p className="text-xs text-slate-400 font-semibold uppercase tracking-wide">Last Update</p>
            <p className="text-sm font-bold text-slate-900">{formattedDate}, {formattedTime}</p>
          </div>
        </div>

        {/* System Status */}
        <div className="flex items-center gap-2">
          <div className="p-1.5 bg-green-100 rounded-lg">
            <Activity size={14} className="text-green-600" strokeWidth={2.5} />
          </div>
          <div>
            <p className="text-xs text-slate-400 font-semibold uppercase tracking-wide">System</p>
            <div className="flex items-center gap-1.5">
              <div className={`w-2 h-2 rounded-full ${data ? 'bg-green-500 animate-pulse' : 'bg-slate-400'}`} />
              <span className="text-sm font-bold text-green-700">{data ? data.system_status : 'Connecting...'}</span>
            </div>
          </div>
        </div>

        {/* User Count */}
        <div className="flex items-center gap-2">
          <div className="p-1.5 bg-purple-100 rounded-lg">
            <Users size={14} className="text-purple-600" strokeWidth={2.5} />
          </div>
          <div>
            <p className="text-xs text-slate-400 font-semibold uppercase tracking-wide">Active Users</p>
            <p className="text-sm font-bold text-slate-900">{data ? data.active_users : '-'}</p>
          </div>
        </div>
      </div>

      {/* Right Section - OK Status */}
      <div className="flex items-center gap-2 px-4 py-2 bg-green-100 border border-green-200 rounded-lg shadow-sm">
        <div className="w-2 h-2 rounded-full bg-green-500" />
        <span className="text-sm font-extrabold text-green-700">{data && data.alerts.critical > 0 ? 'CRITICAL ALERT' : 'ALL SYSTEMS OK'}</span>
      </div>
    </div>
  )
}
