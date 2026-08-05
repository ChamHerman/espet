"""
ESPET Web Dashboard UI Router

Provides a modern, rich, glassmorphic web interface at GET /
for checking backend server health, Google OAuth login status, and exploring all available MCP tools.
"""

from fastapi import APIRouter
from fastapi.responses import HTMLResponse

router = APIRouter(tags=["dashboard"])

DASHBOARD_HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESPET — Embodied AI Agent Hub</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-dark: #070a12;
            --bg-card: rgba(20, 27, 45, 0.65);
            --bg-card-hover: rgba(28, 38, 62, 0.85);
            --border-color: rgba(255, 255, 255, 0.08);
            --border-highlight: rgba(52, 211, 153, 0.35);
            --accent-emerald: #10b981;
            --accent-blue: #3b82f6;
            --accent-purple: #8b5cf6;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --text-dim: #64748b;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            font-family: 'Outfit', sans-serif;
            background-color: var(--bg-dark);
            background-image: 
                radial-gradient(at 10% 10%, rgba(59, 130, 246, 0.15) 0px, transparent 50%),
                radial-gradient(at 90% 10%, rgba(139, 92, 246, 0.12) 0px, transparent 50%),
                radial-gradient(at 50% 90%, rgba(16, 185, 129, 0.1) 0px, transparent 60%);
            background-attachment: fixed;
            color: var(--text-main);
            min-height: 100vh;
            padding: 2rem;
            line-height: 1.5;
        }

        .container {
            max-width: 1250px;
            margin: 0 auto;
        }

        /* ── Header ── */
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 2rem;
            border-bottom: 1px solid var(--border-color);
            margin-bottom: 2rem;
        }

        .logo-area {
            display: flex;
            align-items: center;
            gap: 1rem;
        }

        .logo-icon {
            width: 46px;
            height: 46px;
            border-radius: 12px;
            background: linear-gradient(135deg, var(--accent-blue), var(--accent-emerald));
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1.5rem;
            font-weight: 700;
            color: white;
            box-shadow: 0 0 25px rgba(16, 185, 129, 0.3);
        }

        .logo-title h1 {
            font-size: 1.6rem;
            font-weight: 700;
            letter-spacing: -0.5px;
            background: linear-gradient(90deg, #fff, #94a3b8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .logo-title p {
            font-size: 0.85rem;
            color: var(--text-muted);
        }

        .status-pill {
            display: inline-flex;
            align-items: center;
            gap: 0.6rem;
            padding: 0.4rem 1rem;
            background: rgba(16, 185, 129, 0.12);
            border: 1px solid rgba(16, 185, 129, 0.3);
            border-radius: 9999px;
            font-size: 0.85rem;
            font-weight: 600;
            color: #34d399;
        }

        .pulse-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: #34d399;
            box-shadow: 0 0 10px #34d399;
            animation: pulse 2s infinite;
        }

        @keyframes pulse {
            0% { transform: scale(0.95); opacity: 0.8; }
            50% { transform: scale(1.3); opacity: 1; }
            100% { transform: scale(0.95); opacity: 0.8; }
        }

        /* ── Top Grid Cards ── */
        .grid-cards {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
            gap: 1.5rem;
            margin-bottom: 2.5rem;
        }

        .card {
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            border-radius: 1.25rem;
            padding: 1.75rem;
            backdrop-filter: blur(12px);
            transition: transform 0.25s ease, border-color 0.25s ease, box-shadow 0.25s ease;
            position: relative;
            overflow: hidden;
        }

        .card:hover {
            transform: translateY(-4px);
            border-color: rgba(255, 255, 255, 0.18);
            box-shadow: 0 12px 30px -10px rgba(0, 0, 0, 0.5);
        }

        .card-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1.25rem;
        }

        .card-title {
            font-size: 1.1rem;
            font-weight: 600;
            color: var(--text-main);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }

        .card-icon {
            font-size: 1.4rem;
        }

        /* Auth Card Specific */
        .auth-status-badge {
            font-size: 0.8rem;
            font-weight: 600;
            padding: 0.25rem 0.75rem;
            border-radius: 0.5rem;
            display: inline-flex;
            align-items: center;
            gap: 0.4rem;
        }

        .badge-success {
            background: rgba(16, 185, 129, 0.15);
            color: #34d399;
            border: 1px solid rgba(16, 185, 129, 0.3);
        }

        .badge-warning {
            background: rgba(245, 158, 11, 0.15);
            color: #fbbf24;
            border: 1px solid rgba(245, 158, 11, 0.3);
        }

        .auth-body {
            display: flex;
            flex-direction: column;
            gap: 1rem;
        }

        .auth-desc {
            font-size: 0.9rem;
            color: var(--text-muted);
        }

        .btn-google {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 0.75rem;
            background: linear-gradient(135deg, #2563eb, #1d4ed8);
            color: white;
            padding: 0.75rem 1.25rem;
            border-radius: 0.75rem;
            font-weight: 600;
            font-size: 0.95rem;
            text-decoration: none;
            transition: all 0.2s ease;
            box-shadow: 0 4px 15px rgba(37, 99, 235, 0.3);
            border: none;
            cursor: pointer;
        }

        .btn-google:hover {
            background: linear-gradient(135deg, #3b82f6, #2563eb);
            box-shadow: 0 6px 20px rgba(59, 130, 246, 0.4);
            transform: translateY(-1px);
        }

        /* Health / MCP Servers Card */
        .stat-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 1rem;
            text-align: center;
        }

        .stat-box {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            padding: 1rem 0.5rem;
            border-radius: 0.75rem;
        }

        .stat-num {
            font-size: 1.6rem;
            font-weight: 700;
            color: #38bdf8;
            font-family: 'JetBrains Mono', monospace;
        }

        .stat-label {
            font-size: 0.75rem;
            color: var(--text-muted);
            margin-top: 0.25rem;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .mcp-list {
            display: flex;
            flex-direction: column;
            gap: 0.75rem;
        }

        .mcp-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: rgba(255, 255, 255, 0.03);
            padding: 0.75rem 1rem;
            border-radius: 0.75rem;
            border: 1px solid var(--border-color);
        }

        .mcp-name {
            font-weight: 500;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }

        .mcp-badge {
            font-size: 0.75rem;
            padding: 0.2rem 0.6rem;
            border-radius: 9999px;
            background: rgba(59, 130, 246, 0.15);
            color: #60a5fa;
            border: 1px solid rgba(59, 130, 246, 0.3);
            font-family: 'JetBrains Mono', monospace;
        }

        /* ── Tools Explorer Section ── */
        .tools-section {
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            border-radius: 1.25rem;
            padding: 2rem;
            backdrop-filter: blur(12px);
        }

        .tools-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 1rem;
            margin-bottom: 1.5rem;
        }

        .tools-title {
            font-size: 1.35rem;
            font-weight: 700;
        }

        .search-area {
            display: flex;
            gap: 0.75rem;
            align-items: center;
            flex-wrap: wrap;
        }

        .search-input {
            background: rgba(15, 23, 42, 0.8);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 0.75rem;
            padding: 0.65rem 1rem;
            color: var(--text-main);
            font-family: 'Outfit', sans-serif;
            font-size: 0.9rem;
            width: 260px;
            transition: all 0.2s ease;
        }

        .search-input:focus {
            outline: none;
            border-color: var(--accent-blue);
            box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.2);
        }

        .filter-pills {
            display: flex;
            gap: 0.5rem;
        }

        .filter-btn {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid var(--border-color);
            color: var(--text-muted);
            padding: 0.45rem 0.9rem;
            border-radius: 0.6rem;
            font-size: 0.82rem;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.2s ease;
        }

        .filter-btn:hover {
            color: var(--text-main);
            border-color: rgba(255, 255, 255, 0.2);
        }

        .filter-btn.active {
            background: rgba(59, 130, 246, 0.2);
            color: #60a5fa;
            border-color: rgba(59, 130, 246, 0.5);
        }

        /* Tool Items Grid */
        .tools-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(380px, 1fr));
            gap: 1rem;
        }

        .tool-card {
            background: rgba(15, 23, 42, 0.55);
            border: 1px solid var(--border-color);
            border-radius: 0.85rem;
            padding: 1.25rem;
            transition: all 0.2s ease;
            display: flex;
            flex-direction: column;
            justify-content: space-between;
        }

        .tool-card:hover {
            border-color: rgba(59, 130, 246, 0.4);
            background: rgba(30, 41, 59, 0.6);
        }

        .tool-top {
            margin-bottom: 0.85rem;
        }

        .tool-badge-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 0.6rem;
        }

        .tool-name {
            font-family: 'JetBrains Mono', monospace;
            font-weight: 600;
            font-size: 0.95rem;
            color: #38bdf8;
            background: rgba(56, 189, 248, 0.1);
            padding: 0.25rem 0.6rem;
            border-radius: 0.4rem;
            border: 1px solid rgba(56, 189, 248, 0.2);
        }

        .tool-category {
            font-size: 0.72rem;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            font-weight: 600;
            padding: 0.15rem 0.5rem;
            border-radius: 9999px;
            background: rgba(139, 92, 246, 0.15);
            color: #a78bfa;
            border: 1px solid rgba(139, 92, 246, 0.3);
        }

        .tool-desc {
            font-size: 0.85rem;
            color: var(--text-muted);
            line-height: 1.4;
            display: -webkit-box;
            -webkit-line-clamp: 3;
            -webkit-box-orient: vertical;
            overflow: hidden;
        }

        .tool-footer {
            margin-top: 0.75rem;
            padding-top: 0.75rem;
            border-top: 1px solid rgba(255, 255, 255, 0.05);
            display: flex;
            justify-content: flex-end;
        }

        .btn-schema {
            background: transparent;
            border: none;
            color: #60a5fa;
            font-size: 0.8rem;
            font-weight: 500;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            gap: 0.3rem;
            font-family: 'Outfit', sans-serif;
        }

        .btn-schema:hover {
            color: #93c5fd;
            text-decoration: underline;
        }

        /* Modal for JSON schema */
        .modal-backdrop {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0, 0, 0, 0.75);
            backdrop-filter: blur(8px);
            display: none;
            align-items: center;
            justify-content: center;
            z-index: 100;
            padding: 1.5rem;
        }

        .modal {
            background: #0f172a;
            border: 1px solid rgba(255, 255, 255, 0.15);
            border-radius: 1.25rem;
            width: 100%;
            max-width: 650px;
            max-height: 80vh;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.7);
        }

        .modal-header {
            padding: 1.25rem 1.5rem;
            border-bottom: 1px solid var(--border-color);
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        .modal-title {
            font-family: 'JetBrains Mono', monospace;
            font-size: 1.1rem;
            color: #38bdf8;
            font-weight: 600;
        }

        .btn-close {
            background: transparent;
            border: none;
            color: var(--text-muted);
            font-size: 1.5rem;
            cursor: pointer;
            line-height: 1;
        }

        .btn-close:hover {
            color: var(--text-main);
        }

        .modal-body {
            padding: 1.5rem;
            overflow-y: auto;
        }

        pre {
            background: #070a12;
            padding: 1.25rem;
            border-radius: 0.75rem;
            border: 1px solid var(--border-color);
            color: #e2e8f0;
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.82rem;
            overflow-x: auto;
        }

        /* ── Responsive ── */
        @media (max-width: 768px) {
            body { padding: 1rem; }
            header { flex-direction: column; align-items: flex-start; gap: 1rem; }
            .tools-header { flex-direction: column; align-items: flex-start; }
            .search-input { width: 100%; }
        }
    </style>
</head>
<body>

<div class="container">
    <!-- Header -->
    <header>
        <div class="logo-area">
            <div class="logo-icon">E</div>
            <div class="logo-title">
                <h1>ESPET AI Agent Hub</h1>
                <p>Embodied Sensing & MCP Backend Control Dashboard</p>
            </div>
        </div>
        <div id="status-pill" class="status-pill">
            <div class="pulse-dot"></div>
            <span id="header-status-text">Server Healthy</span>
        </div>
    </header>

    <!-- Top Grid -->
    <div class="grid-cards">
        <!-- Card 1: Auth Status -->
        <div class="card">
            <div class="card-header">
                <div class="card-title">
                    <span class="card-icon">🔐</span>
                    <span>Google OAuth Authentication</span>
                </div>
                <div id="auth-badge" class="auth-status-badge badge-warning">
                    ⏳ Checking...
                </div>
            </div>
            <div class="auth-body">
                <p id="auth-desc" class="auth-desc">
                    Checking Google OAuth token status in secure database...
                </p>
                <div>
                    <a href="/auth/google" class="btn-google">
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor">
                            <path d="M22.56 12.25c0-.78-.07-1.53-.2-2.25H12v4.26h5.92c-.26 1.37-1.04 2.53-2.21 3.31v2.77h3.57c2.08-1.92 3.28-4.74 3.28-8.09z" fill="#4285F4"/>
                            <path d="M12 23c2.97 0 5.46-.98 7.28-2.66l-3.57-2.77c-.98.66-2.23 1.06-3.71 1.06-2.86 0-5.29-1.93-6.16-4.53H2.18v2.84C3.99 20.53 7.7 23 12 23z" fill="#34A853"/>
                            <path d="M5.84 14.09c-.22-.66-.35-1.36-.35-2.09s.13-1.43.35-2.09V7.06H2.18C1.43 8.55 1 10.22 1 12s.43 3.45 1.18 4.94l2.85-2.22.81-.63z" fill="#FBBC05"/>
                            <path d="M12 5.38c1.62 0 3.06.56 4.21 1.64l3.15-3.15C17.45 2.09 14.97 1 12 1 7.7 1 3.99 3.47 2.18 7.06l3.66 2.84c.87-2.6 3.3-4.52 6.16-4.52z" fill="#EA4335"/>
                        </svg>
                        <span>Sign in with Google OAuth</span>
                    </a>
                </div>
            </div>
        </div>

        <!-- Card 2: Health Overview -->
        <div class="card">
            <div class="card-header">
                <div class="card-title">
                    <span class="card-icon">⚡</span>
                    <span>Backend Server Health</span>
                </div>
                <span class="auth-status-badge badge-success">● Active 0.0.0.0:8000</span>
            </div>
            <div class="stat-grid">
                <div class="stat-box">
                    <div id="stat-total-tools" class="stat-num">-</div>
                    <div class="stat-label">MCP Tools</div>
                </div>
                <div class="stat-box">
                    <div id="stat-servers" class="stat-num">-</div>
                    <div class="stat-label">Servers</div>
                </div>
                <div class="stat-box">
                    <div class="stat-num" style="color: #34d399;">200</div>
                    <div class="stat-label">Status OK</div>
                </div>
            </div>
        </div>

        <!-- Card 3: Connected MCP Servers -->
        <div class="card">
            <div class="card-header">
                <div class="card-title">
                    <span class="card-icon">🌐</span>
                    <span>Connected Google MCPs</span>
                </div>
            </div>
            <div id="mcp-list" class="mcp-list">
                <div class="mcp-item">
                    <span class="mcp-name">⏳ Loading servers...</span>
                </div>
            </div>
        </div>
    </div>

    <!-- Tools Explorer -->
    <div class="tools-section">
        <div class="tools-header">
            <div class="tools-title">Available Remote MCP Tools (<span id="tools-count">0</span>)</div>
            <div class="search-area">
                <input type="text" id="search-input" class="search-input" placeholder="Search tool by name or function...">
                <div class="filter-pills">
                    <button class="filter-btn active" data-filter="all">All</button>
                    <button class="filter-btn" data-filter="gmail">Gmail</button>
                    <button class="filter-btn" data-filter="calendar">Calendar</button>
                    <button class="filter-btn" data-filter="drive">Drive</button>
                </div>
            </div>
        </div>

        <div id="tools-grid" class="tools-grid">
            <div style="color: var(--text-muted); padding: 2rem 0;">Loading tools from backend...</div>
        </div>
    </div>
</div>

<!-- Modal for Schema -->
<div id="schema-modal" class="modal-backdrop">
    <div class="modal">
        <div class="modal-header">
            <div id="modal-title" class="modal-title">Tool Schema</div>
            <button id="modal-close" class="btn-close">&times;</button>
        </div>
        <div class="modal-body">
            <pre><code id="modal-json">{}</code></pre>
        </div>
    </div>
</div>

<script>
    let allTools = [];
    let currentFilter = 'all';

    async function loadDashboard() {
        try {
            // 1. Check Auth Status
            const authRes = await fetch('/auth/status');
            const authData = await authRes.json();
            const authBadge = document.getElementById('auth-badge');
            const authDesc = document.getElementById('auth-desc');

            if (authData.authenticated) {
                const acc = authData.email || 'Google Account';
                authBadge.className = 'auth-status-badge badge-success';
                authBadge.innerHTML = '✅ ' + acc;
                authDesc.innerHTML = `Signed in as <strong style="color:#fff; font-size:1.02rem; text-decoration:underline;">${acc}</strong>. All Gmail, Calendar, and Drive tools are ready for your ESP32.`;
            } else {
                authBadge.className = 'auth-status-badge badge-warning';
                authBadge.innerHTML = '⚠️ Not Signed In';
                authDesc.innerHTML = `No OAuth credentials found. Click the button below to authorize Google Gmail, Calendar, and Drive access.`;
            }

            // 2. Check Health
            const healthRes = await fetch('/health');
            const healthData = await healthRes.json();
            
            document.getElementById('stat-total-tools').textContent = healthData.total_tools || 31;
            const serversObj = healthData.mcp_servers || {};
            const serverKeys = Object.keys(serversObj);
            document.getElementById('stat-servers').textContent = serverKeys.length || 3;

            // Render MCP Servers list
            const mcpListEl = document.getElementById('mcp-list');
            mcpListEl.innerHTML = '';
            for (const [srvName, info] of Object.entries(serversObj)) {
                const srvTitle = srvName.replace('google-', 'Google ');
                const icon = srvName.includes('gmail') ? '📧' : srvName.includes('calendar') ? '📅' : '📁';
                mcpListEl.innerHTML += `
                    <div class="mcp-item">
                        <span class="mcp-name"><span>${icon}</span> <span>${srvTitle}</span></span>
                        <span class="mcp-badge">${info.tool_count} tools • Connected</span>
                    </div>
                `;
            }

            // 3. Load Tools
            const toolsRes = await fetch('/api/tools', {
                headers: { 'Authorization': 'Bearer espet_secret_device_key_2026' }
            });
            const toolsData = await toolsRes.json();
            allTools = toolsData.tools || [];
            document.getElementById('tools-count').textContent = allTools.length;

            renderTools();

        } catch (err) {
            console.error('Failed to load dashboard data:', err);
            document.getElementById('header-status-text').textContent = 'Server Unreachable';
        }
    }

    function getCategory(toolName) {
        if (toolName.includes('gmail') || toolName.includes('thread') || toolName.includes('draft') || toolName.includes('message') || toolName.includes('label')) {
            return 'gmail';
        }
        if (toolName.includes('event') || toolName.includes('calendar') || toolName.includes('time')) {
            return 'calendar';
        }
        if (toolName.includes('file') || toolName.includes('drive')) {
            return 'drive';
        }
        return 'system';
    }

    function renderTools() {
        const grid = document.getElementById('tools-grid');
        const searchTerm = document.getElementById('search-input').value.toLowerCase();

        const filtered = allTools.filter(t => {
            const matchesSearch = t.name.toLowerCase().includes(searchTerm) || 
                                  (t.description && t.description.toLowerCase().includes(searchTerm));
            const cat = getCategory(t.name);
            const matchesCat = currentFilter === 'all' || cat === currentFilter;
            return matchesSearch && matchesCat;
        });

        if (filtered.length === 0) {
            grid.innerHTML = `<div style="color: var(--text-muted); grid-column: 1/-1; padding: 2rem 0;">No matching tools found.</div>`;
            return;
        }

        grid.innerHTML = filtered.map((t, idx) => {
            const cat = getCategory(t.name);
            const catLabel = cat.toUpperCase();
            return `
                <div class="tool-card">
                    <div class="tool-top">
                        <div class="tool-badge-row">
                            <span class="tool-name">${t.name}</span>
                            <span class="tool-category">${catLabel}</span>
                        </div>
                        <div class="tool-desc">${t.description || 'No description provided.'}</div>
                    </div>
                    <div class="tool-footer">
                        <button class="btn-schema" onclick="showSchema(${idx})">
                            <span>View JSON Schema</span>
                            <span>&rarr;</span>
                        </button>
                    </div>
                </div>
            `;
        }).join('');
    }

    // Modal
    const modal = document.getElementById('schema-modal');
    function showSchema(idx) {
        const t = allTools[idx];
        document.getElementById('modal-title').textContent = t.name + ' — Input Schema';
        document.getElementById('modal-json').textContent = JSON.stringify(t.input_schema || {}, null, 2);
        modal.style.display = 'flex';
    }

    document.getElementById('modal-close').onclick = () => modal.style.display = 'none';
    modal.onclick = (e) => { if (e.target === modal) modal.style.display = 'none'; };

    // Search and Filters
    document.getElementById('search-input').addEventListener('input', renderTools);
    document.querySelectorAll('.filter-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));
            e.target.classList.add('active');
            currentFilter = e.target.getAttribute('data-filter');
            renderTools();
        });
    });

    // Auto-load on ready
    window.addEventListener('DOMContentLoaded', loadDashboard);
    // Refresh every 10 seconds
    setInterval(loadDashboard, 10000);
</script>
</body>
</html>
"""


@router.get(
    "/",
    response_class=HTMLResponse,
    summary="ESPET AI Agent Web Dashboard",
    description="Interactive dashboard showing auth status, server health, and MCP tools explorer.",
)
async def dashboard():
    return HTMLResponse(content=DASHBOARD_HTML)
