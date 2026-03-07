import { useState, useRef, useEffect } from 'react'
import { User, LogOut, ChevronDown, Lock } from 'lucide-react'
import { useAuth } from '../context/AuthContext'
import ProfileModal from './Modals/ProfileModal'
import PasswordModal from './Modals/PasswordModal'
import { motion, AnimatePresence } from 'framer-motion'

export default function Header() {
  const { user, logout } = useAuth()
  const [isMenuOpen, setIsMenuOpen] = useState(false)
  const [showProfile, setShowProfile] = useState(false)
  const [showPassword, setShowPassword] = useState(false)

  const menuRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (menuRef.current && !menuRef.current.contains(event.target as Node)) {
        setIsMenuOpen(false)
      }
    }
    document.addEventListener("mousedown", handleClickOutside)
    return () => document.removeEventListener("mousedown", handleClickOutside)
  }, [menuRef])

  return (
    <>
      <header className="bg-white border-b border-gray-200 sticky top-0 z-20 shadow-sm">
        <div className="px-8 py-5 flex items-center justify-between">
          <div>
            <h2 className="text-2xl font-extrabold text-gray-900">Waste Bin Monitoring</h2>
            <p className="text-sm text-gray-500 mt-1 font-medium">Real-time bin monitoring dashboard</p>
          </div>

          <div className="relative" ref={menuRef}>
            <button
              onClick={() => setIsMenuOpen(!isMenuOpen)}
              className="flex items-center space-x-3 p-2 hover:bg-slate-50 rounded-xl transition-all border border-transparent hover:border-slate-200"
            >
              <div className="w-10 h-10 bg-green-100 rounded-full flex items-center justify-center text-green-700 font-bold text-lg">
                {user?.name?.charAt(0) || 'U'}
              </div>
              <div className="hidden md:block text-left">
                <p className="text-sm font-bold text-slate-800">{user?.name || 'User'}</p>
                <p className="text-xs text-slate-500 font-medium">{user?.email || ''}</p>
              </div>
              <ChevronDown className={`w-4 h-4 text-slate-400 transition-transform ${isMenuOpen ? 'rotate-180' : ''}`} />
            </button>

            <AnimatePresence>
              {isMenuOpen && (
                <motion.div
                  initial={{ opacity: 0, y: 10, scale: 0.95 }}
                  animate={{ opacity: 1, y: 0, scale: 1 }}
                  exit={{ opacity: 0, y: 10, scale: 0.95 }}
                  transition={{ duration: 0.1 }}
                  className="absolute right-0 mt-2 w-56 bg-white rounded-xl shadow-xl border border-slate-100 py-2 z-50 overflow-hidden"
                >
                  <div className="px-4 py-3 border-b border-slate-50 mb-1">
                    <p className="text-xs font-bold text-slate-400 uppercase tracking-wider">Account</p>
                  </div>

                  <button
                    onClick={() => { setShowProfile(true); setIsMenuOpen(false); }}
                    className="w-full text-left px-4 py-2.5 text-sm text-slate-700 hover:bg-slate-50 hover:text-green-600 flex items-center transition-colors font-medium"
                  >
                    <User className="w-4 h-4 mr-3" />
                    Profile Settings
                  </button>

                  <button
                    onClick={() => { setShowPassword(true); setIsMenuOpen(false); }}
                    className="w-full text-left px-4 py-2.5 text-sm text-slate-700 hover:bg-slate-50 hover:text-green-600 flex items-center transition-colors font-medium"
                  >
                    <Lock className="w-4 h-4 mr-3" />
                    Change Password
                  </button>

                  <div className="my-1 border-t border-slate-50"></div>

                  <button
                    onClick={logout}
                    className="w-full text-left px-4 py-2.5 text-sm text-red-600 hover:bg-red-50 flex items-center transition-colors font-bold"
                  >
                    <LogOut className="w-4 h-4 mr-3" />
                    Sign Out
                  </button>
                </motion.div>
              )}
            </AnimatePresence>
          </div>
        </div>
      </header>

      <ProfileModal isOpen={showProfile} onClose={() => setShowProfile(false)} />
      <PasswordModal isOpen={showPassword} onClose={() => setShowPassword(false)} />
    </>
  )
}
