import { BrowserRouter as Router, Routes, Route, Navigate, useNavigate } from 'react-router-dom'
import { AuthProvider, useAuth } from './context/AuthContext'
import Dashboard from './components/Dashboard'
import AuthPage from './pages/AuthPage'
import ProfileSettings from './components/ProfileSettings'
import './App.css'

function AppRoutes() {
  const { user, loading } = useAuth();
  const navigate = useNavigate();

  if (loading) {
    return <div className="flex h-screen items-center justify-center bg-slate-50">Loading Smart System...</div>
  }

  return (
    <Routes>
      <Route
        path="/login"
        element={!user ? <AuthPage onLogin={() => { }} /> : <Navigate to="/" />}
      />
      <Route
        path="/"
        element={user ? <Dashboard onSettings={() => navigate('/settings')} /> : <Navigate to="/login" />}
      />
      <Route
        path="/settings"
        element={user ? <ProfileSettings onBack={() => navigate('/')} /> : <Navigate to="/login" />}
      />
    </Routes>
  );
}

export default function App() {
  return (
    <AuthProvider>
      <Router>
        <AppRoutes />
      </Router>
    </AuthProvider>
  )
}
