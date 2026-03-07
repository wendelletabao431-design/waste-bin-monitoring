import { useState } from 'react'
import { motion } from 'framer-motion'
import { Mail, Lock, User, ArrowRight, Loader2, Eye, EyeOff } from 'lucide-react'
import { GoogleLogin } from '@react-oauth/google'
import { useAuth } from '../../context/AuthContext'

interface RegisterFormProps {
    onRegister: () => void
}

export default function RegisterForm({ onRegister }: RegisterFormProps) {
    const { register, googleLogin } = useAuth()
    const [loading, setLoading] = useState(false)
    const [googleLoading, setGoogleLoading] = useState(false)
    const [name, setName] = useState('')
    const [email, setEmail] = useState('')
    const [password, setPassword] = useState('')
    const [showPassword, setShowPassword] = useState(false)
    const [error, setError] = useState('')

    const handleSubmit = async (e: React.FormEvent) => {
        e.preventDefault()
        setError('')
        setLoading(true)
        try {
            await register({ name, email, password }, onRegister)
        } catch (err: any) {
            console.error(err);
            setError(err.message || JSON.stringify(err) || 'Registration failed');
        } finally {
            setLoading(false)
        }
    }

    const handleGoogleSuccess = async (credentialResponse: any) => {
        setError('')
        setGoogleLoading(true)
        try {
            await googleLogin(credentialResponse.credential, onRegister)
        } catch (err: any) {
            console.error(err);
            setError(err.message || 'Google login failed');
        } finally {
            setGoogleLoading(false)
        }
    }

    const handleGoogleError = () => {
        setError('Google login failed. Please try again.')
    }

    const inputVariants = {
        hidden: { opacity: 0, x: 20 },
        visible: { opacity: 1, x: 0 }
    }

    return (
        <motion.div
            initial="hidden"
            animate="visible"
            exit={{ opacity: 0, x: 20 }}
            transition={{ staggerChildren: 0.1 }}
            className="space-y-4"
        >
            <form onSubmit={handleSubmit} className="space-y-4">
                {error && (
                    <div className="bg-red-50 text-red-600 text-sm p-3 rounded-lg border border-red-100">
                        {error}
                    </div>
                )}

                <motion.div variants={inputVariants} className="space-y-1">
                    <label className="block text-sm font-medium text-slate-500 ml-1">Full Name</label>
                    <div className="relative">
                        <div className="absolute inset-y-0 left-0 pl-3 flex items-center pointer-events-none">
                            <User className="h-5 w-5 text-slate-400" />
                        </div>
                        <input
                            type="text"
                            required
                            value={name}
                            onChange={e => setName(e.target.value)}
                            className="block w-full pl-10 pr-10 py-3 border border-slate-200 rounded-2xl leading-5 bg-slate-50 text-slate-900 caret-slate-900 placeholder-slate-400 focus:outline-none focus:bg-white focus:ring-2 focus:ring-green-500 focus:border-green-500 transition-all duration-200 sm:text-sm"
                            placeholder="John Doe"
                        />
                    </div>
                </motion.div>

                <motion.div variants={inputVariants} className="space-y-1">
                    <label className="block text-sm font-medium text-slate-500 ml-1">Email</label>
                    <div className="relative">
                        <div className="absolute inset-y-0 left-0 pl-3 flex items-center pointer-events-none">
                            <Mail className="h-5 w-5 text-slate-400" />
                        </div>
                        <input
                            type="email"
                            required
                            value={email}
                            onChange={e => setEmail(e.target.value)}
                            className="block w-full pl-10 pr-10 py-3 border border-slate-200 rounded-2xl leading-5 bg-slate-50 text-slate-900 caret-slate-900 placeholder-slate-400 focus:outline-none focus:bg-white focus:ring-2 focus:ring-green-500 focus:border-green-500 transition-all duration-200 sm:text-sm"
                            placeholder="you@example.com"
                        />
                    </div>
                </motion.div>

                <motion.div variants={inputVariants} className="space-y-1">
                    <label className="block text-sm font-medium text-slate-500 ml-1">Password</label>
                    <div className="relative">
                        <div className="absolute inset-y-0 left-0 pl-3 flex items-center pointer-events-none">
                            <Lock className="h-5 w-5 text-slate-400" />
                        </div>
                        <input
                            type={showPassword ? "text" : "password"}
                            required
                            value={password}
                            onChange={e => setPassword(e.target.value)}
                            className="block w-full pl-10 pr-10 py-3 border border-slate-200 rounded-2xl leading-5 bg-slate-50 text-slate-900 caret-slate-900 placeholder-slate-400 focus:outline-none focus:bg-white focus:ring-2 focus:ring-green-500 focus:border-green-500 transition-all duration-200 sm:text-sm"
                            placeholder="••••••••"
                        />
                        <button
                            type="button"
                            onClick={() => setShowPassword(!showPassword)}
                            className="absolute inset-y-0 right-0 pr-3 flex items-center text-slate-400 hover:text-slate-600 transition-colors cursor-pointer"
                        >
                            {showPassword ? <EyeOff className="h-5 w-5" /> : <Eye className="h-5 w-5" />}
                        </button>
                    </div>
                </motion.div>

                <motion.button
                    variants={inputVariants}
                    whileHover={{ scale: 1.02, backgroundColor: '#16a34a' }}
                    whileTap={{ scale: 0.98 }}
                    type="submit"
                    disabled={loading}
                    className="w-full flex justify-center items-center py-3 px-4 border border-transparent rounded-2xl shadow-sm text-sm font-bold text-white bg-green-500 hover:bg-green-600 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-green-500 transition-colors mt-2 disabled:opacity-70 disabled:cursor-not-allowed"
                >
                    {loading ? <Loader2 className="animate-spin h-5 w-5" /> : (
                        <>
                            Create Account
                            <ArrowRight className="ml-2 h-4 w-4" />
                        </>
                    )}
                </motion.button>
            </form>

            <motion.div variants={inputVariants} className="mt-4">
                <div className="relative">
                    <div className="absolute inset-0 flex items-center">
                        <div className="w-full border-t border-slate-200" />
                    </div>
                    <div className="relative flex justify-center text-sm">
                        <span className="px-2 bg-white text-slate-500">Or continue with</span>
                    </div>
                </div>
                <div className="mt-4 flex justify-center">
                    {googleLoading ? (
                        <div className="flex items-center justify-center py-2">
                            <Loader2 className="animate-spin h-5 w-5 text-slate-400" />
                        </div>
                    ) : (
                        <GoogleLogin
                            onSuccess={handleGoogleSuccess}
                            onError={handleGoogleError}
                            useOneTap
                            theme="outline"
                            size="large"
                            width="300"
                        />
                    )}
                </div>
            </motion.div>
        </motion.div>
    )
}
