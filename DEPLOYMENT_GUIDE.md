# Deployment Guide: Smart Trash Bin System

This guide will help you deploy your Laravel backend to Railway.app (free) so your ESP32 can communicate with it over the internet.

---

## Option 1: Railway.app (Recommended - Free)

### Step 1: Create GitHub Repository

First, push your code to GitHub:

```bash
# Navigate to your project
cd C:\Users\ProfitVault\Downloads\TrashBinProj

# Initialize git (if not already)
git init

# Create .gitignore for backend
echo "vendor/" >> backend/.gitignore
echo ".env" >> backend/.gitignore
echo "node_modules/" >> backend/.gitignore

# Add all files
git add .
git commit -m "Initial commit - Smart Trash Bin System"

# Create repo on GitHub, then:
git remote add origin https://github.com/YOUR_USERNAME/smart-trash-bin.git
git branch -M main
git push -u origin main
```

### Step 2: Sign Up for Railway

1. Go to **https://railway.app**
2. Click "Login" → "Login with GitHub"
3. Authorize Railway to access your GitHub

### Step 3: Create New Project

1. Click **"New Project"**
2. Select **"Deploy from GitHub repo"**
3. Choose your `smart-trash-bin` repository
4. Select the `backend` folder as the root directory

### Step 4: Add MySQL Database

1. In your Railway project, click **"New"**
2. Select **"Database"** → **"Add MySQL"**
3. Wait for it to provision (takes ~30 seconds)

### Step 5: Configure Environment Variables

1. Click on your Laravel service
2. Go to **"Variables"** tab
3. Click **"RAW Editor"** and paste:

```env
APP_NAME=SmartTrashBin
APP_ENV=production
APP_DEBUG=false
APP_KEY=

DB_CONNECTION=mysql
DB_HOST=${{MySQL.MYSQLHOST}}
DB_PORT=${{MySQL.MYSQLPORT}}
DB_DATABASE=${{MySQL.MYSQLDATABASE}}
DB_USERNAME=${{MySQL.MYSQLUSER}}
DB_PASSWORD=${{MySQL.MYSQLPASSWORD}}

SESSION_DRIVER=cookie
CACHE_STORE=file
QUEUE_CONNECTION=sync
```

4. Generate APP_KEY locally and add it:
```bash
cd backend
php artisan key:generate --show
# Copy the output (base64:xxxx...) and add as APP_KEY
```

### Step 6: Deploy

1. Railway will auto-deploy when you push to GitHub
2. Or click **"Deploy"** manually
3. Wait 2-3 minutes for build

### Step 7: Get Your Public URL

1. Click on your Laravel service
2. Go to **"Settings"** → **"Networking"**
3. Click **"Generate Domain"**
4. You'll get a URL like: `https://smart-trash-bin-production.up.railway.app`

### Step 8: Update ESP32 Firmware

Update the `serverURL` in your ESP32 code:

```cpp
const char* serverURL = "https://smart-trash-bin-production.up.railway.app/api/bin-data";
```

---

## Option 2: Render.com (Free Alternative)

### Step 1: Sign Up
1. Go to **https://render.com**
2. Sign up with GitHub

### Step 2: Create Web Service
1. Click **"New"** → **"Web Service"**
2. Connect your GitHub repo
3. Configure:
   - **Root Directory**: `backend`
   - **Build Command**: `composer install --no-dev && php artisan migrate --force`
   - **Start Command**: `php artisan serve --host=0.0.0.0 --port=$PORT`

### Step 3: Add PostgreSQL Database
1. Click **"New"** → **"PostgreSQL"**
2. Create free database
3. Copy connection details to environment variables

### Step 4: Update for PostgreSQL
Change `DB_CONNECTION=pgsql` in environment variables.

---

## Option 3: Ngrok (For Testing Only)

If you just want to test temporarily without deploying:

### Step 1: Install Ngrok
```bash
# Windows (with chocolatey)
choco install ngrok

# Or download from https://ngrok.com/download
```

### Step 2: Start Laravel Locally
```bash
cd backend
php artisan serve --host=0.0.0.0 --port=8000
```

### Step 3: Expose with Ngrok
```bash
ngrok http 8000
```

### Step 4: Use the Ngrok URL
You'll get a URL like: `https://abc123.ngrok.io`

Update ESP32:
```cpp
const char* serverURL = "https://abc123.ngrok.io/api/bin-data";
```

**Note**: Ngrok URLs change every time you restart. Not suitable for production.

---

## Frontend Deployment (React)

### Deploy to Vercel (Free)

1. Go to **https://vercel.com**
2. Sign up with GitHub
3. Click **"New Project"**
4. Select your repo
5. Configure:
   - **Root Directory**: `trash-bin-butler`
   - **Framework**: Vite
6. Add environment variable:
   ```
   VITE_API_URL=https://your-railway-app.up.railway.app/api
   ```
7. Deploy!

### Update Frontend API URL

Edit `trash-bin-butler/src/services/api.ts`:

```typescript
const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:8000/api';
```

---

## Testing Your Deployment

### Test ESP32 Endpoint

```bash
curl -X POST https://your-railway-app.up.railway.app/api/bin-data \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "TEST:AA:BB:CC:DD:EE",
    "battery_voltage": 11.82,
    "bin_1": {"distance_cm": 59.5, "hx711_raw": 50000, "mq_raw": 200},
    "bin_2": {"distance_cm": 60.0, "hx711_raw": 30000, "mq_raw": 150}
  }'
```

Expected response:
```json
{
  "status": "ok",
  "message": "Data received and processed",
  "device_id": "TEST:AA:BB:CC:DD:EE",
  "battery_percent": 45.2,
  "bins": {...}
}
```

### Test Dashboard Endpoint

```bash
curl https://your-railway-app.up.railway.app/api/devices
```

---

## Troubleshooting

### "Connection Refused" from ESP32
- Check WiFi connection on ESP32
- Verify the URL is correct (include `https://`)
- Make sure Railway app is deployed and running

### "500 Internal Server Error"
- Check Railway logs: Click on service → "Logs"
- Verify database connection
- Run migrations: Add to build command

### "CORS Error" in Browser
- The `config/cors.php` allows all origins
- If still issues, check browser console for specific error

### ESP32 SSL Issues
If ESP32 can't connect to HTTPS:
```cpp
// Add this before http.begin()
WiFiClientSecure client;
client.setInsecure(); // Skip certificate verification

HTTPClient http;
http.begin(client, serverURL);
```

---

## Cost Summary

| Service | Free Tier | Paid |
|---------|-----------|------|
| Railway | $5/month credit | $0.01/GB/month |
| Render | 750 hours/month | $7/month |
| Vercel | Unlimited | $20/month |
| Ngrok | 1 tunnel, temp URLs | $10/month |

**For a capstone project, the free tiers are sufficient!**

---

## Architecture After Deployment

```
┌──────────────────┐     HTTPS      ┌─────────────────────────────────┐
│    ESP32         │───────────────▶│  Railway.app                    │
│  (Your Hardware) │                │  Laravel Backend                │
│                  │                │  POST /api/bin-data             │
└──────────────────┘                └────────────┬────────────────────┘
                                                 │
                                                 │ MySQL
                                                 ▼
┌──────────────────┐     HTTPS      ┌─────────────────────────────────┐
│    Browser       │◀──────────────▶│  Vercel                         │
│  (Dashboard)     │                │  React Frontend                 │
│                  │                │  GET /api/devices               │
└──────────────────┘                └─────────────────────────────────┘
```
