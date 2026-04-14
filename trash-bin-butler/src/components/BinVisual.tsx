import type { Bin } from '../types/bin'

interface BinVisualProps {
  bin: Bin
}

export default function BinVisual({ bin }: BinVisualProps) {
  const percentage = bin.fill

  // Dynamic colors based on fill level
  const getBinColor = () => {
    if (percentage >= 90) return { primary: '#ef4444', secondary: '#dc2626', accent: '#b91c1c' } // Red
    if (percentage >= 50) return { primary: '#f59e0b', secondary: '#d97706', accent: '#b45309' } // Orange
    return { primary: '#22c55e', secondary: '#16a34a', accent: '#15803d' } // Green
  }

  const colors = getBinColor()

  return (
    <div className="flex flex-col items-center">
      <h3 className="text-sm font-semibold text-slate-400 uppercase tracking-wide mb-4">Bin Status</h3>

      <div className="relative w-40 h-52 mb-8">
        {/* Modern Trash Bin SVG - Responsive Design */}
        <svg viewBox="0 0 200 300" className="w-full h-full">
          <defs>
            {/* Gradient for bin body */}
            <linearGradient id={`binGradient-${bin.id}`} x1="0%" y1="0%" x2="0%" y2="100%">
              <stop offset="0%" style={{ stopColor: colors.primary, stopOpacity: 0.9 }} />
              <stop offset="100%" style={{ stopColor: colors.secondary, stopOpacity: 1 }} />
            </linearGradient>

            {/* Shine effect */}
            <linearGradient id={`shine-${bin.id}`} x1="0%" y1="0%" x2="100%" y2="0%">
              <stop offset="0%" style={{ stopColor: 'white', stopOpacity: 0 }} />
              <stop offset="50%" style={{ stopColor: 'white', stopOpacity: 0.3 }} />
              <stop offset="100%" style={{ stopColor: 'white', stopOpacity: 0 }} />
            </linearGradient>

            {/* Fill level gradient */}
            <linearGradient id={`fillGradient-${bin.id}`} x1="0%" y1="0%" x2="0%" y2="100%">
              <stop offset="0%" style={{ stopColor: colors.primary, stopOpacity: 0.4 }} />
              <stop offset="100%" style={{ stopColor: colors.accent, stopOpacity: 0.7 }} />
            </linearGradient>
          </defs>

          {/* Shadow */}
          <ellipse cx="100" cy="280" rx="50" ry="8" fill="#94a3b8" opacity="0.3" />

          {/* Main bin body - tapered design */}
          <path
            d="M 55 75 L 65 245 Q 100 255 135 245 L 145 75 Z"
            fill={`url(#binGradient-${bin.id})`}
            stroke={colors.accent}
            strokeWidth="2.5"
          />

          {/* Shine effect on bin */}
          <path
            d="M 70 80 L 75 240 L 72 240 L 67 80 Z"
            fill={`url(#shine-${bin.id})`}
            opacity="0.6"
          />

          {/* Fill level indicator */}
          <defs>
            <clipPath id={`binClip-${bin.id}`}>
              <path d="M 57 77 L 66 243 L 134 243 L 143 77 Z" />
            </clipPath>
          </defs>
          <rect
            x="57"
            y={245 - (percentage * 1.68)}
            width="86"
            height={percentage * 1.68}
            fill={`url(#fillGradient-${bin.id})`}
            clipPath={`url(#binClip-${bin.id})`}
          />

          {/* Lid rim */}
          <rect
            x="40"
            y="65"
            width="120"
            height="12"
            rx="6"
            fill={colors.primary}
            stroke={colors.accent}
            strokeWidth="2"
          />

          {/* Lid top with shine */}
          <ellipse
            cx="100"
            cy="70"
            rx="50"
            ry="12"
            fill={colors.secondary}
            stroke={colors.accent}
            strokeWidth="2.5"
          />

          {/* Lid handle */}
          <path
            d="M 80 58 Q 100 40 120 58"
            fill="none"
            stroke={colors.accent}
            strokeWidth="5"
            strokeLinecap="round"
          />
          <path
            d="M 80 58 Q 100 42 120 58"
            fill="none"
            stroke={colors.primary}
            strokeWidth="3"
            strokeLinecap="round"
          />

          {/* Recycling symbol - adaptive color */}
          <g transform="translate(100, 155)">
            {/* Triangle arrows forming recycling symbol */}
            <path
              d="M 0,-15 L -13,7 L -7,7 L -7,0 L 0,0 Z"
              fill="white"
              opacity="0.85"
            />
            <path
              d="M 0,-15 L 13,7 L 7,7 L 7,0 L 0,0 Z"
              fill="white"
              opacity="0.85"
              transform="rotate(120)"
            />
            <path
              d="M 0,-15 L -13,7 L -7,7 L -7,0 L 0,0 Z"
              fill="white"
              opacity="0.85"
              transform="rotate(240)"
            />
          </g>

          {/* Bin texture lines */}
          <line x1="60" y1="100" x2="140" y2="100" stroke={colors.accent} strokeWidth="1" opacity="0.2" />
          <line x1="62" y1="150" x2="138" y2="150" stroke={colors.accent} strokeWidth="1" opacity="0.2" />
          <line x1="64" y1="200" x2="136" y2="200" stroke={colors.accent} strokeWidth="1" opacity="0.2" />
        </svg>

        {/* Fill Level Badge - Dynamic Color */}
        <div
          className="absolute top-1/2 left-1/2 transform -translate-x-1/2 -translate-y-1/2 text-white rounded-full w-20 h-20 flex items-center justify-center shadow-lg border-4 border-white"
          style={{ backgroundColor: colors.primary }}
        >
          <div className="text-center">
            <div className="text-2xl font-extrabold leading-none">{percentage}%</div>
          </div>
        </div>
      </div>

      {/* Status Legend - Compact */}
      <div className="space-y-2 w-full">
        <div className="flex items-center justify-between px-3 py-2 bg-green-50 rounded-lg border border-green-200">
          <div className="flex items-center gap-2">
            <div className="w-2 h-2 rounded-full bg-green-500" />
            <span className="text-xs font-semibold text-slate-700">Normal</span>
          </div>
          <span className="text-xs text-slate-500">0-49%</span>
        </div>
        <div className="flex items-center justify-between px-3 py-2 bg-yellow-50 rounded-lg border border-yellow-200">
          <div className="flex items-center gap-2">
            <div className="w-2 h-2 rounded-full bg-yellow-500" />
            <span className="text-xs font-semibold text-slate-700">Warning</span>
          </div>
          <span className="text-xs text-slate-500">50-89%</span>
        </div>
        <div className="flex items-center justify-between px-3 py-2 bg-red-50 rounded-lg border border-red-200">
          <div className="flex items-center gap-2">
            <div className="w-2 h-2 rounded-full bg-red-500" />
            <span className="text-xs font-semibold text-slate-700">Critical</span>
          </div>
          <span className="text-xs text-slate-500">90%+</span>
        </div>
      </div>
    </div>
  )
}
