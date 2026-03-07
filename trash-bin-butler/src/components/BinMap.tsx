import type { Bin } from '../types/bin'

const statusColors: Record<string, string> = {
  Normal: '#22c55e',
  Warning: '#eab308',
  Critical: '#ef4444',
  Offline: '#6b7280',
}

interface BinMapProps {
  bin: Bin
}

function isValidCoordinate(value: number) {
  return Number.isFinite(value) && value !== 0
}

function hasUsableLocation(location?: string) {
  if (!location) {
    return false
  }

  const normalized = location.trim().toLowerCase()
  return normalized !== ''
    && normalized !== 'waiting for device...'
    && normalized !== 'unknown'
    && normalized !== 'unassigned location'
}

function buildOpenStreetMapEmbedUrl(lat: number, lng: number) {
  const padding = 0.0035
  const bbox = [
    lng - padding,
    lat - padding,
    lng + padding,
    lat + padding,
  ]
    .map((value) => value.toFixed(6))
    .join('%2C')

  return `https://www.openstreetmap.org/export/embed.html?bbox=${bbox}&layer=mapnik&marker=${lat.toFixed(6)}%2C${lng.toFixed(6)}`
}

export default function BinMap({ bin }: BinMapProps) {
  const lat = Number(bin.latitude)
  const lng = Number(bin.longitude)
  const hasCoordinates = isValidCoordinate(lat) && isValidCoordinate(lng)
  const hasLocation = hasUsableLocation(bin.location)

  const embedUrl = hasCoordinates ? buildOpenStreetMapEmbedUrl(lat, lng) : ''
  const openMapUrl = hasCoordinates
    ? `https://www.openstreetmap.org/?mlat=${lat.toFixed(6)}&mlon=${lng.toFixed(6)}#map=17/${lat.toFixed(6)}/${lng.toFixed(6)}`
    : hasLocation
      ? `https://www.openstreetmap.org/search?query=${encodeURIComponent(bin.location)}`
      : ''

  return (
    <div className="bg-white rounded-2xl shadow-md p-6">
      <div className="mb-4 flex items-start justify-between gap-4">
        <div>
          <h3 className="text-lg font-semibold text-slate-800">Bin Location</h3>
          <p className="mt-1 text-sm text-slate-500">{bin.location}</p>
        </div>

        {openMapUrl ? (
          <a
            href={openMapUrl}
            target="_blank"
            rel="noreferrer"
            className="inline-flex shrink-0 items-center rounded-lg border border-slate-200 px-3 py-2 text-sm font-medium text-slate-700 transition hover:border-slate-300 hover:bg-slate-50"
          >
            {hasCoordinates ? 'Open Street Map' : 'Search location'}
          </a>
        ) : null}
      </div>

      <div className="h-[300px] overflow-hidden rounded-xl border border-slate-200 bg-slate-100">
        {hasCoordinates ? (
          <iframe
            title={`${bin.name} location map`}
            src={embedUrl}
            className="h-full w-full border-0"
            loading="lazy"
          />
        ) : (
          <div className="flex h-full flex-col items-center justify-center gap-3 px-6 text-center text-slate-600">
            <p className="text-base font-medium text-slate-700">Map unavailable</p>
            <p className="max-w-md text-sm text-slate-500">
              This bin does not have coordinates yet. Add a saved location or let the backend provide coordinates for it.
            </p>
          </div>
        )}
      </div>

      <div className="mt-3 flex items-center gap-2 text-sm text-slate-600">
        <span
          className="inline-block h-3 w-3 rounded-full"
          style={{ backgroundColor: statusColors[bin.status] || statusColors.Offline }}
        />
        <span>{bin.name}</span>
        <span className="text-slate-300">|</span>
        <span>{hasCoordinates ? `${lat.toFixed(6)}, ${lng.toFixed(6)}` : 'Coordinates unavailable'}</span>
      </div>
    </div>
  )
}
