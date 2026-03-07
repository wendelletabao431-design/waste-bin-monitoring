export function getStatus(fill: number): 'Normal' | 'Warning' | 'Critical' {
  if (fill <= 70) return 'Normal'
  if (fill <= 94) return 'Warning'
  return 'Critical'
}

export function getStatusColor(fill: number): string {
  if (fill <= 70) return '#22c55e'
  if (fill <= 94) return '#eab308'
  return '#ef4444'
}

export function getStatusLabel(fill: number): string {
  if (fill <= 70) return 'Normal'
  if (fill <= 94) return 'Getting Full'
  return 'Full'
}

export function getStatusBgColor(fill: number): string {
  if (fill <= 70) return '#dcfce7'
  if (fill <= 94) return '#fef3c7'
  return '#fee2e2'
}

export function getStatusColorText(fill: number): string {
  if (fill <= 70) return '#166534'
  if (fill <= 94) return '#b45309'
  return '#991b1b'
}

export function getGasColor(gas: string): string {
  if (gas === 'Normal') return '#22c55e'
  if (gas === 'Elevated') return '#eab308'
  return '#ef4444'
}
