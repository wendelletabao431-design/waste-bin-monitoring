import { useState, useEffect } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import type { Bin } from '../types/bin'
import Sidebar from './Sidebar'
import Header from './Header'
import BinVisual from './BinVisual'
import BinDetails from './BinDetails'
import BinMap from './BinMap'
import MetricsGrid from './MetricsGrid'
import SystemStatus from './SystemStatus'
import { api } from '../services/api'

interface DashboardProps {
    onSettings?: () => void
}

export default function Dashboard({ onSettings }: DashboardProps) {
    const DEFAULT_BIN: Bin = {
        id: 0,
        name: "Bin #1",
        location: "Cafeteria, Building A-1",
        fill: 0,
        battery: 0,
        gas: "Normal",
        weight: 0,
        status: "Offline",
        latitude: 11.237934,
        longitude: 124.999284
    }

    const [bins, setBins] = useState<Bin[]>([DEFAULT_BIN])
    const [selectedBin, setSelectedBin] = useState<Bin | null>(DEFAULT_BIN)

    useEffect(() => {
        void loadDevices()

        // Poll every 10 seconds
        const interval = setInterval(() => {
            void loadDevices()
        }, 10000)
        return () => clearInterval(interval)
    }, [])

    const loadDevices = async () => {
        try {
            const data = await api.fetchDevices()

            // If API returns empty, stick with DEFAULT_BIN
            const binsToDisplay = data && data.length > 0 ? data : [DEFAULT_BIN]

            setBins(binsToDisplay)

            // Preserve the user's current selection across polling updates.
            setSelectedBin((currentSelectedBin) => {
                if (!currentSelectedBin || currentSelectedBin.id === 0) {
                    return binsToDisplay[0]
                }

                const updated = binsToDisplay.find((b: Bin) => b.id === currentSelectedBin.id)
                return updated ?? binsToDisplay[0]
            })
        } catch (err) {
            console.error(err)
            // On error, we keep the current state (which might be DEFAULT_BIN)
        }
    }

    // Removed blocking loading state so dashboard is always visible

    return (
        <div className="flex h-screen bg-slate-50">
            {selectedBin && (
                <Sidebar bins={bins} selectedBin={selectedBin} onSelectBin={setSelectedBin} onSettings={onSettings} />
            )}

            <main className="flex-1 flex flex-col overflow-hidden">
                <Header />

                <div className="flex-1 overflow-y-auto p-6">
                    <div className="max-w-7xl mx-auto space-y-6">
                        {selectedBin ? (
                            <>
                                <AnimatePresence mode="wait">
                                    <motion.div
                                        key={selectedBin.id}
                                        initial={{ opacity: 0, y: 10 }}
                                        animate={{ opacity: 1, y: 0 }}
                                        exit={{ opacity: 0, y: -10 }}
                                        transition={{ duration: 0.2 }}
                                        className="space-y-6"
                                    >
                                        {/* Top Priority: Bin Info + Metrics Grid */}
                                        <div className="bg-white rounded-xl shadow-md p-6">
                                            <div className="mb-6">
                                                <h2 className="text-2xl font-extrabold text-slate-900 mb-1">{selectedBin.name}</h2>
                                                <p className="text-sm text-slate-500">{selectedBin.location}</p>
                                            </div>
                                            <MetricsGrid bin={selectedBin} />
                                        </div>

                                        {/* Main Content: Side-by-Side Layout */}
                                        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
                                            {/* Left: Compact Bin Visual (30%) */}
                                            <div className="lg:col-span-1 bg-white rounded-xl shadow-md p-6">
                                                <BinVisual bin={selectedBin} />
                                            </div>

                                            {/* Right: Detailed Bin Information (70%) */}
                                            <div className="lg:col-span-2 bg-white rounded-xl shadow-md p-6">
                                                <BinDetails bin={selectedBin} />
                                            </div>
                                        </div>

                                        {/* Map Section */}
                                        <BinMap bin={selectedBin} />
                                    </motion.div>
                                </AnimatePresence>
                            </>
                        ) : (
                            <div className="text-center p-12 text-slate-500">No bins found.</div>
                        )}

                        {/* Bottom: System Status */}
                        <div className="bg-white rounded-xl shadow-md overflow-hidden">
                            <SystemStatus />
                        </div>
                    </div>
                </div>
            </main>
        </div>
    )
}
