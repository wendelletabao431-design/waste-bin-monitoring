import { useState } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import { Recycle } from 'lucide-react'
import LoginForm from './LoginForm'
import RegisterForm from './RegisterForm'

interface AuthCardProps {
    onLogin: () => void
}

export default function AuthCard({ onLogin }: AuthCardProps) {
    const [isLogin, setIsLogin] = useState(true)

    return (
        <motion.div
            initial={{ opacity: 0, y: 50 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.6, type: "spring", bounce: 0.5 }}
            className="w-full max-w-md bg-white rounded-3xl shadow-xl overflow-hidden p-8 relative z-20"
        >
            {/* Header */}
            <div className="text-center mb-8">
                <div className="mx-auto h-12 w-12 bg-green-100 rounded-xl flex items-center justify-center mb-4 transform rotate-3 hover:rotate-6 transition-transform">
                    <Recycle className="h-8 w-8 text-green-600" />
                </div>
                <h2 className="text-2xl font-extrabold text-slate-900 tracking-tight">
                    Waste Bin Monitoring
                </h2>
                <p className="mt-2 text-sm text-slate-500 font-medium">
                    Intelligent City Management System
                </p>
            </div>

            {/* Toggle */}
            <div className="relative bg-slate-100 p-1 rounded-xl flex mb-8">
                <motion.div
                    layout
                    className="absolute inset-y-1 left-1 w-[calc(50%-4px)] bg-green-500 rounded-lg shadow-sm"
                    animate={{ x: isLogin ? 0 : '100%' }}
                    transition={{ type: "spring", bounce: 0.2, duration: 0.3 }}
                />
                <button
                    onClick={() => setIsLogin(true)}
                    className={`relative z-10 w-1/2 py-2 text-sm font-bold transition-colors ${isLogin ? 'text-white' : 'text-slate-500 hover:text-slate-700'
                        }`}
                >
                    Sign In
                </button>
                <button
                    onClick={() => setIsLogin(false)}
                    className={`relative z-10 w-1/2 py-2 text-sm font-bold transition-colors ${!isLogin ? 'text-white' : 'text-slate-500 hover:text-slate-700'
                        }`}
                >
                    Register
                </button>
            </div>

            {/* Forms with Animation */}
            <div className="relative min-h-[300px]">
                {/* Min-height prevents layout jump, generally good but can be dynamic */}
                <AnimatePresence mode="wait">
                    {isLogin ? (
                        <motion.div
                            key="login"
                            initial={{ opacity: 0, x: -20 }}
                            animate={{ opacity: 1, x: 0 }}
                            exit={{ opacity: 0, x: 20 }}
                            transition={{ duration: 0.2 }}
                        >
                            <LoginForm onLogin={onLogin} />
                        </motion.div>
                    ) : (
                        <motion.div
                            key="register"
                            initial={{ opacity: 0, x: 20 }}
                            animate={{ opacity: 1, x: 0 }}
                            exit={{ opacity: 0, x: -20 }}
                            transition={{ duration: 0.2 }}
                        >
                            <RegisterForm onRegister={onLogin} />
                        </motion.div>
                    )}
                </AnimatePresence>
            </div>

            {/* Footer */}
            <div className="mt-6 text-center">
                <p className="text-xs text-slate-400">
                    Powered by IoT & Green Tech
                </p>
            </div>
        </motion.div>
    )
}
