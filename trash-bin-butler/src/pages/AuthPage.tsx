import AuthCard from '../components/Auth/AuthCard'

interface AuthPageProps {
    onLogin: () => void
}

export default function AuthPage({ onLogin }: AuthPageProps) {
    return (
        <div className="min-h-screen bg-gray-100 flex flex-col justify-center items-center p-4 relative overflow-hidden">
            {/* Background decoration */}
            <div className="absolute top-0 left-0 w-full h-full overflow-hidden pointer-events-none">
                <div className="absolute -top-20 -left-20 w-64 h-64 rounded-full bg-green-200 opacity-20 filter blur-3xl"></div>
                <div className="absolute top-40 right-10 w-72 h-72 rounded-full bg-blue-200 opacity-20 filter blur-3xl"></div>
                <div className="absolute bottom-10 left-1/3 w-80 h-80 rounded-full bg-orange-200 opacity-20 filter blur-3xl"></div>
            </div>

            <AuthCard onLogin={onLogin} />

            <div className="mt-8 text-center text-slate-400 text-xs relative z-10">
                &copy; {new Date().getFullYear()} Waste Bin Monitoring. All rights reserved.
            </div>
        </div>
    )
}
