// Use environment variable in production, fallback to localhost for development
const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:8000/api';

const getHeaders = () => {
    const token = localStorage.getItem('token');
    return {
        'Content-Type': 'application/json',
        'Accept': 'application/json',
        ...(token ? { 'Authorization': `Bearer ${token}` } : {})
    };
};

export const api = {
    // Auth
    register: async (data: any) => {
        const res = await fetch(`${API_URL}/register`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
            body: JSON.stringify(data)
        });
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || 'Registration failed');
        return json;
    },

    login: async (data: any) => {
        const res = await fetch(`${API_URL}/login`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
            body: JSON.stringify(data)
        });
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || json.email?.[0] || 'Login failed');
        return json;
    },

    googleLogin: async (idToken: string) => {
        const res = await fetch(`${API_URL}/auth/google`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
            body: JSON.stringify({ id_token: idToken })
        });
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || 'Google login failed');
        return json;
    },

    logout: async () => {
        await fetch(`${API_URL}/logout`, {
            method: 'POST',
            headers: getHeaders()
        });
    },

    getUser: async () => {
        const res = await fetch(`${API_URL}/user`, {
            headers: getHeaders()
        });
        if (!res.ok) throw new Error('Failed to valid session');
        return res.json();
    },

    updateProfile: async (data: any) => {
        const res = await fetch(`${API_URL}/user/profile`, {
            method: 'PUT',
            headers: getHeaders(),
            body: JSON.stringify(data)
        });
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || 'Update failed');
        return json;
    },

    updatePassword: async (data: any) => {
        const res = await fetch(`${API_URL}/user/password`, {
            method: 'PUT',
            headers: getHeaders(),
            body: JSON.stringify(data)
        });
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || 'Update failed');
        return json;
    },

    // Dashboard
    fetchSummary: async () => {
        const res = await fetch(`${API_URL}/dashboard/summary`, { headers: getHeaders() });
        if (!res.ok) throw new Error('Failed to fetch summary');
        return res.json();
    },

    fetchDevices: async () => {
        const res = await fetch(`${API_URL}/devices`, { headers: getHeaders() });
        if (!res.ok) throw new Error('Failed to fetch devices');
        return res.json();
    },

    fetchDeviceDetails: async (id: number) => {
        const res = await fetch(`${API_URL}/devices/${id}/details`, { headers: getHeaders() });
        if (!res.ok) throw new Error('Failed to fetch details');
        return res.json();
    },

    updateNotifications: async (enabled: boolean) => {
        const res = await fetch(`${API_URL}/user/notifications`, {
            method: 'PUT',
            headers: getHeaders(),
            body: JSON.stringify({ notification_enabled: enabled })
        });
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || 'Failed to update notifications');
        return json;
    },

    deleteAccount: async () => {
        const res = await fetch(`${API_URL}/user/account`, {
            method: 'DELETE',
            headers: getHeaders()
        });
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || 'Failed to delete account');
        return json;
    }
};
