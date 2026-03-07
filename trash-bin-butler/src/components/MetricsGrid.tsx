import { Gauge, Wind, Weight, BatteryLow, BatteryMedium, BatteryFull } from 'lucide-react'
import type { Bin } from '../types/bin'
import { getStatusColor, getGasColor } from '../utils/binStatus'

interface MetricsGridProps {
  bin: Bin
}

export default function MetricsGrid({ bin }: MetricsGridProps) {
  // Determine battery icon and color based on level
  const getBatteryIcon = () => {
    if (bin.battery <= 30) return { Icon: BatteryLow, color: '#ef4444', bgColor: 'bg-red-50', borderColor: 'border-red-200' }
    if (bin.battery <= 70) return { Icon: BatteryMedium, color: '#f59e0b', bgColor: 'bg-orange-50', borderColor: 'border-orange-200' }
    return { Icon: BatteryFull, color: '#22c55e', bgColor: 'bg-green-50', borderColor: 'border-green-200' }
  }

  const batteryData = getBatteryIcon()

  const metrics = [
    {
      icon: Gauge,
      label: 'Waste Level',
      value: `${bin.fill}%`,
      color: getStatusColor(bin.fill),
      iconColor: '#f59e0b',
      bgColor: 'bg-orange-50',
      borderColor: 'border-orange-200'
    },
    {
      icon: Wind,
      label: 'Gas Level',
      value: bin.gas,
      color: getGasColor(bin.gas),
      iconColor: '#f59e0b',
      bgColor: bin.gas === 'Normal' ? 'bg-green-50' : 'bg-orange-50',
      borderColor: bin.gas === 'Normal' ? 'border-green-200' : 'border-orange-200'
    },
    {
      icon: Weight,
      label: 'Weight',
      value: `${bin.weight} kg`,
      color: '#0ea5e9',
      iconColor: '#0ea5e9',
      bgColor: 'bg-sky-50',
      borderColor: 'border-sky-200'
    },
    {
      icon: batteryData.Icon,
      label: 'Battery',
      value: `${bin.battery}%`,
      color: batteryData.color,
      iconColor: batteryData.color,
      bgColor: batteryData.bgColor,
      borderColor: batteryData.borderColor
    }
  ]

  return (
    <div className="grid grid-cols-4 gap-4">
      {metrics.map((metric, idx) => {
        const Icon = metric.icon
        return (
          <div
            key={idx}
            className={`${metric.bgColor} rounded-xl p-4 border ${metric.borderColor} transition-all hover:shadow-md cursor-default`}
          >
            <Icon size={28} color={metric.iconColor} className="mb-3" strokeWidth={2.5} />
            <p className="text-xs font-semibold text-slate-400 uppercase tracking-wide mb-1">
              {metric.label}
            </p>
            <p className="text-3xl font-extrabold" style={{ color: metric.color }}>
              {metric.value}
            </p>
          </div>
        )
      })}
    </div>
  )
}
