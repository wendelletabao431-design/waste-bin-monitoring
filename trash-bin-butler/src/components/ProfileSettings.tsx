import { useState } from 'react';
import { motion } from 'framer-motion';
import { User, Bell, Trash2, ArrowLeft, Loader2 } from 'lucide-react';
import { useAuth } from '../context/AuthContext';

interface ProfileSettingsProps {
    onBack: () => void;
}

export default function ProfileSettings({ onBack }: ProfileSettingsProps) {
    const { user, updateNotifications, deleteAccount } = useAuth();
    const [notifications, setNotifications] = useState(user?.notification_enabled ?? true);
    const [saving, setSaving] = useState(false);
    const [showDeleteConfirm, setShowDeleteConfirm] = useState(false);
    const [deleting, setDeleting] = useState(false);
    const [message, setMessage] = useState<{ type: 'success' | 'error'; text: string } | null>(null);

    const handleNotificationToggle = async () => {
        setSaving(true);
        setMessage(null);
        try {
            const newValue = !notifications;
            await updateNotifications(newValue);
            setNotifications(newValue);
            setMessage({ type: 'success', text: 'Notification preference updated!' });
        } catch (err: any) {
            setMessage({ type: 'error', text: err.message || 'Failed to update notifications' });
        } finally {
            setSaving(false);
        }
    };

    const handleDeleteAccount = async () => {
        setDeleting(true);
        try {
            await deleteAccount();
        } catch (err: any) {
            setMessage({ type: 'error', text: err.message || 'Failed to delete account' });
            setDeleting(false);
        }
    };

    return (
        <div className="min-h-screen bg-slate-50 p-6">
            <div className="max-w-2xl mx-auto">
                <button
                    onClick={onBack}
                    className="flex items-center gap-2 text-slate-600 hover:text-slate-900 mb-6 transition-colors"
                >
                    <ArrowLeft className="h-5 w-5" />
                    <span className="font-medium">Back to Dashboard</span>
                </button>

                <motion.div
                    initial={{ opacity: 0, y: 20 }}
                    animate={{ opacity: 1, y: 0 }}
                    className="bg-white rounded-2xl shadow-md overflow-hidden"
                >
                    <div className="bg-gradient-to-r from-green-500 to-emerald-600 p-6">
                        <h1 className="text-2xl font-bold text-white">Profile Settings</h1>
                        <p className="text-green-100 mt-1">Manage your account preferences</p>
                    </div>

                    <div className="p-6 space-y-6">
                        {message && (
                            <div className={`p-4 rounded-lg ${message.type === 'success' ? 'bg-green-50 text-green-700 border border-green-200' : 'bg-red-50 text-red-700 border border-red-200'}`}>
                                {message.text}
                            </div>
                        )}

                        <div className="flex items-center gap-4 p-4 bg-slate-50 rounded-xl">
                            <div className="w-16 h-16 bg-gradient-to-br from-green-400 to-emerald-500 rounded-full flex items-center justify-center">
                                <User className="h-8 w-8 text-white" />
                            </div>
                            <div className="flex-1">
                                <h2 className="text-lg font-semibold text-slate-900">{user?.name}</h2>
                                <p className="text-slate-500">{user?.email}</p>
                            </div>
                        </div>

                        <div className="border-t border-slate-200 pt-6">
                            <h3 className="text-lg font-semibold text-slate-900 mb-4 flex items-center gap-2">
                                <Bell className="h-5 w-5 text-green-500" />
                                Notifications
                            </h3>
                            <div className="flex items-center justify-between p-4 bg-slate-50 rounded-xl">
                                <div>
                                    <p className="font-medium text-slate-900">Email Notifications</p>
                                    <p className="text-sm text-slate-500">Receive email alerts when bins need attention</p>
                                </div>
                                <button
                                    onClick={handleNotificationToggle}
                                    disabled={saving}
                                    className={`relative inline-flex h-7 w-12 items-center rounded-full transition-colors ${notifications ? 'bg-green-500' : 'bg-slate-300'}`}
                                >
                                    {saving ? (
                                        <span className="inline-block h-5 w-5 transform rounded-full bg-white shadow-md flex items-center justify-center">
                                            <Loader2 className="h-3 w-3 animate-spin text-green-500" />
                                        </span>
                                    ) : (
                                        <span className={`inline-block h-5 w-5 transform rounded-full bg-white shadow-md transition-transform ${notifications ? 'translate-x-6' : 'translate-x-1'}`} />
                                    )}
                                </button>
                            </div>
                        </div>

                        <div className="border-t border-slate-200 pt-6">
                            <h3 className="text-lg font-semibold text-red-600 mb-4 flex items-center gap-2">
                                <Trash2 className="h-5 w-5" />
                                Danger Zone
                            </h3>
                            
                            {!showDeleteConfirm ? (
                                <button
                                    onClick={() => setShowDeleteConfirm(true)}
                                    className="px-4 py-2 bg-red-50 text-red-600 rounded-lg border border-red-200 hover:bg-red-100 transition-colors font-medium"
                                >
                                    Delete Account
                                </button>
                            ) : (
                                <div className="p-4 bg-red-50 rounded-xl border border-red-200">
                                    <p className="text-red-700 font-medium mb-4">
                                        Are you sure? This action cannot be undone. All your data will be permanently deleted.
                                    </p>
                                    <div className="flex gap-3">
                                        <button
                                            onClick={() => setShowDeleteConfirm(false)}
                                            disabled={deleting}
                                            className="px-4 py-2 bg-slate-100 text-slate-700 rounded-lg hover:bg-slate-200 transition-colors font-medium"
                                        >
                                            Cancel
                                        </button>
                                        <button
                                            onClick={handleDeleteAccount}
                                            disabled={deleting}
                                            className="px-4 py-2 bg-red-600 text-white rounded-lg hover:bg-red-700 transition-colors font-medium flex items-center gap-2"
                                        >
                                            {deleting ? (
                                                <>
                                                    <Loader2 className="h-4 w-4 animate-spin" />
                                                    Deleting...
                                                </>
                                            ) : (
                                                'Yes, Delete My Account'
                                            )}
                                        </button>
                                    </div>
                                </div>
                            )}
                        </div>
                    </div>
                </motion.div>
            </div>
        </div>
    );
}
