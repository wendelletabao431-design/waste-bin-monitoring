export interface Bin {
  id: number
  name: string
  fill: number
  location: string
  latitude?: number
  longitude?: number
  gas: 'Normal' | 'Elevated' | 'Dangerous'
  weight: number
  battery: number
  status: 'Normal' | 'Warning' | 'Critical' | 'Offline'
}

export type Status = 'Normal' | 'Warning' | 'Critical'

export interface BinSummary {
  Normal: number
  Warning: number
  Critical: number
}
