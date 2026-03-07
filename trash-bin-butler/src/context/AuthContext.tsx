import React, { createContext, useContext, useState, useEffect } from 'react';
import { api } from '../services/api';

interface User {
    id: number;
    name: string;
    email: string;
    notification_enabled?: boolean;
}

interface AuthContextType {
    user: User | null;
    loading: boolean;
    login: (data: any, callback?: () => void) => Promise<void>;
    register: (data: any, callback?: () => void) => Promise<void>;
    googleLogin: (idToken: string, callback?: () => void) => Promise<void>;
    logout: () => void;
    updateUser: (data: any) => Promise<void>;
    updateNotifications: (enabled: boolean) => Promise<void>;
    deleteAccount: () => Promise<void>;
}

const AuthContext = createContext<AuthContextType | undefined>(undefined);

export function AuthProvider({ children }: { children: React.ReactNode }) {
    const [user, setUser] = useState<User | null>(null);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        const token = localStorage.getItem('token');
        if (token) {
            api.getUser()
                .then(data => setUser(data))
                .catch(() => {
                    localStorage.removeItem('token');
                    setUser(null);
                })
                .finally(() => setLoading(false));
        } else {
            setLoading(false);
        }
    }, []);

    const login = async (data: any, callback?: () => void) => {
        const res = await api.login(data);
        localStorage.setItem('token', res.access_token);
        setUser(res.user);
        if (callback) callback();
    };

    const register = async (data: any, callback?: () => void) => {
        const res = await api.register(data);
        localStorage.setItem('token', res.access_token);
        setUser(res.user);
        if (callback) callback();
    };

    const googleLogin = async (idToken: string, callback?: () => void) => {
        const res = await api.googleLogin(idToken);
        localStorage.setItem('token', res.access_token);
        setUser(res.user);
        if (callback) callback();
    };

    const logout = async () => {
        try {
            await api.logout();
        } catch (e) {
            console.error(e);
        }
        localStorage.removeItem('token');
        setUser(null);
    };

    const updateUser = async (data: any) => {
        const res = await api.updateProfile(data);
        setUser(res.user);
    }

    const updateNotifications = async (enabled: boolean) => {
        const res = await api.updateNotifications(enabled);
        setUser(prev => prev ? { ...prev, notification_enabled: enabled } : null);
        return res;
    }

    const deleteAccount = async () => {
        await api.deleteAccount();
        localStorage.removeItem('token');
        setUser(null);
    }

    return (
        <AuthContext.Provider value={{ user, loading, login, register, googleLogin, logout, updateUser, updateNotifications, deleteAccount }}>
            {children}
        </AuthContext.Provider>
    );
}

export function useAuth() {
    const context = useContext(AuthContext);
    if (context === undefined) {
        throw new Error('useAuth must be used within an AuthProvider');
    }
    return context;
}
