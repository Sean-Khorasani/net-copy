#pragma once

#include <string>

namespace netcopy {
namespace gui {

inline std::string build_gui_html() {
    std::string html;
    html.reserve(130000);
    html += R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NetCopy GUI Client</title>
    <!-- Fonts -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
    <!-- Tailwind CSS -->
    <script src="https://cdn.tailwindcss.com"></script>
    <script>
        tailwind.config = {
            theme: {
                extend: {
                    fontFamily: {
                        sans: ['Outfit', 'sans-serif'],
                        mono: ['JetBrains Mono', 'monospace'],
                    },
                    boxShadow: {
                        'neon-blue': '0 0 15px rgba(59, 130, 246, 0.4)',
                        'neon-green': '0 0 15px rgba(16, 185, 129, 0.4)',
                        'neon-red': '0 0 15px rgba(239, 68, 68, 0.4)',
                    }
                }
            }
        }
    </script>
    <!-- React & Babel -->
    <script src="https://unpkg.com/react@18/umd/react.production.min.js" crossorigin></script>
    <script src="https://unpkg.com/react-dom@18/umd/react-dom.production.min.js" crossorigin></script>
    <script src="https://unpkg.com/@babel/standalone/babel.min.js"></script>
    <style>
        body {
            background-color: #0b0f19;
            color: #e2e8f0;
            overflow: hidden;
        }
        /* Custom scrollbars */
        ::-webkit-scrollbar {
            width: 8px;
            height: 8px;
        }
        ::-webkit-scrollbar-track {
            background: #0f172a;
        }
        ::-webkit-scrollbar-thumb {
            background: #334155;
            border-radius: 4px;
        }
        ::-webkit-scrollbar-thumb:hover {
            background: #475569;
        }
        .glass-panel {
            background: rgba(15, 23, 42, 0.75);
            backdrop-filter: blur(12px);
            border: 1px solid rgba(51, 65, 85, 0.5);
        }
        .glass-input {
            background: rgba(30, 41, 59, 0.5);
            border: 1px solid rgba(71, 85, 105, 0.5);
        }
        .glass-input:focus {
            border-color: #3b82f6;
            outline: none;
            box-shadow: 0 0 10px rgba(59, 130, 246, 0.2);
        }
        @keyframes blink {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.35; }
        }
        .animate-blink {
            animation: blink 1.2s infinite;
        }
    </style>
</head>
<body>
    <div id="root" class="h-screen w-screen flex flex-col p-4 space-y-4"></div>

    <script type="text/babel">
        const { useState, useEffect, useRef } = React;

        // Custom Inline SVG Icons to avoid loading external icon libraries
        const FolderIcon = () => (
            <svg className="w-5 h-5 text-amber-400 mr-2 flex-shrink-0" fill="currentColor" viewBox="0 0 20 20"><path d="M2 6a2 2 0 012-2h5l2 2h5a2 2 0 012 2v6a2 2 0 01-2 2H4a2 2 0 01-2-2V6z"></path></svg>
        );
        const FileIcon = () => (
            <svg className="w-5 h-5 text-blue-400 mr-2 flex-shrink-0" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M7 21h10a2 2 0 002-2V9.414a1 1 0 00-.293-.707l-5.414-5.414A1 1 0 0012.586 3H7a2 2 0 00-2 2v14a2 2 0 002 2z"></path></svg>
        );
        const DriveIcon = () => (
            <svg className="w-5 h-5 text-slate-400 mr-2 flex-shrink-0" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M5 12h14M5 12a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v4a2 2 0 01-2 2M5 12a2 2 0 00-2 2v4a2 2 0 002 2h14a2 2 0 002-2v-4a2 2 0 00-2-2m-2-4h.01M17 16h.01"></path></svg>
        );
        const ArrowUpIcon = () => (
            <svg className="w-4 h-4 text-slate-300" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M5 10l7-7m0 0l7 7m-7-7v18"></path></svg>
        );
        const RefreshIcon = () => (
            <svg className="w-4 h-4 text-slate-300" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M4 4v5h.582m15.356 2A8.001 8.001 0 1121.2 8H17"></path></svg>
        );
        const SettingsIcon = () => (
            <svg className="w-5 h-5" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z"></path><path strokeLinecap="round" strokeLinejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z"></path></svg>
        );
)rawhtml";
    html += R"rawhtml(        // Formatter utilities
        const formatBytes = (bytes) => {
            if (bytes === 0 || bytes === "0") return '0 B';
            const k = 1024;
            const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
        };

        const formatDate = (timestamp) => {
            if (!timestamp) return '-';
            const date = new Date(timestamp * 1000);
            return date.toLocaleString();
        };

        const PauseIcon = () => (
            <svg className="w-3 h-3 text-cyan-400 mr-1 flex-shrink-0" fill="currentColor" viewBox="0 0 24 24">
                <path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"/>
            </svg>
        );
        const PlayIcon = () => (
            <svg className="w-3 h-3 text-emerald-400 mr-1 flex-shrink-0" fill="currentColor" viewBox="0 0 24 24">
                <path d="M8 5v14l11-7z"/>
            </svg>
        );
        const SkipIcon = () => (
            <svg className="w-3 h-3 text-red-400 mr-1 flex-shrink-0" fill="currentColor" viewBox="0 0 24 24">
                <path d="M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"/>
            </svg>
        );

        const TransferCard = ({ transfer, index, cardPositions, setCardPositions, hiddenCards, setHiddenCards, onRemove, onShowServerStatus, onControlAll, refreshExplorers }) => {
            const [expanded, setExpanded] = useState(false);
            const [details, setDetails] = useState(null);
            const [showConfirm, setShowConfirm] = useState(false);
            const [isDragging, setIsDragging] = useState(false);
            const [dragStart, setDragStart] = useState({ mouseX: 0, mouseY: 0, cardX: 0, cardY: 0 });

            const handleMouseDown = (e) => {
                if (e.button !== 0) return; // Only drag with left click
                if (e.target.closest('button') || e.target.closest('a') || e.target.closest('input') || e.target.closest('select')) return;

                e.preventDefault();
                
                const currentPos = cardPositions[transfer.id] || {
                    x: Math.max(0, Math.min(window.innerWidth - 384, window.innerWidth - 410 - (index * 24))),
                    y: Math.max(0, Math.min(window.innerHeight - (expanded ? 420 : 160), window.innerHeight - 380 - (index * 24)))
                };

                setDragStart({
                    mouseX: e.clientX,
                    mouseY: e.clientY,
                    cardX: currentPos.x,
                    cardY: currentPos.y
                });
                setIsDragging(true);
            };

            useEffect(() => {
                if (!isDragging) return;

                const handleMouseMove = (e) => {
                    const dx = e.clientX - dragStart.mouseX;
                    const dy = e.clientY - dragStart.mouseY;

                    let newX = dragStart.cardX + dx;
                    let newY = dragStart.cardY + dy;

                    const cardWidth = 384;
                    const cardHeight = expanded ? 420 : 160;

                    newX = Math.max(0, Math.min(window.innerWidth - cardWidth, newX));
                    newY = Math.max(0, Math.min(window.innerHeight - cardHeight, newY));

                    setCardPositions(prev => ({
                        ...prev,
                        [transfer.id]: { x: newX, y: newY }
                    }));
                };

                const handleMouseUp = (e) => {
                    setIsDragging(false);
                    
                    // Click detection: if moved less than 5 pixels, toggle expansion
                    const dx = Math.abs(e.clientX - dragStart.mouseX);
                    const dy = Math.abs(e.clientY - dragStart.mouseY);
                    if (dx < 5 && dy < 5) {
                        setExpanded(!expanded);
                    }
                };

                window.addEventListener('mousemove', handleMouseMove);
                window.addEventListener('mouseup', handleMouseUp);

                return () => {
                    window.removeEventListener('mousemove', handleMouseMove);
                    window.removeEventListener('mouseup', handleMouseUp);
                };
            }, [isDragging, dragStart, expanded]);

            )rawhtml";
    html += R"rawhtml(useEffect(() => {
                let intervalId;
                const fetchDetails = async () => {
                    try {
                        const res = await fetch(`/api/transfer/status?id=${encodeURIComponent(transfer.id)}`);
                        if (res.ok) {
                            const data = await res.json();
                            setDetails(data);
                            if (!data.active && transfer.active) {
                                refreshExplorers();
                            }
                        }
                    } catch (err) {
                        console.error("Error fetching transfer details:", err);
                    }
                };

                if (expanded) {
                    fetchDetails();
                    intervalId = setInterval(fetchDetails, 1000);
                } else {
                    setDetails(null);
                }

                return () => {
                    if (intervalId) clearInterval(intervalId);
                };
            }, [transfer.id, expanded, transfer.active]);

            const handleMinimize = (e) => {
                e.stopPropagation();
                setExpanded(!expanded);
            };

            const handleFileControl = async (e, filePath, action) => {
                e.stopPropagation();
                try {
                    await fetch('/api/transfer/file/control', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ id: transfer.id, path: filePath, action })
                    });
                    const res = await fetch(`/api/transfer/status?id=${encodeURIComponent(transfer.id)}`);
                    if (res.ok) {
                        const data = await res.json();
                        setDetails(data);
                    }
                } catch (err) {
                    console.error("Error sending file control action:", err);
                }
            };

            const hasActiveFiles = () => {
                if (details && details.files) {
                    return details.files.some(f => f.status === 'transferring' || f.status === 'pending');
                }
                return true;
            };

            const percent = transfer.percent || 0;
            const isUpload = transfer.direction === 'upload';
            const progressColor = isUpload ? 'from-blue-500 to-indigo-600' : 'from-emerald-500 to-teal-600';
            const neonShadow = isUpload ? 'shadow-neon-blue' : 'shadow-neon-green';

            if (transfer.minimized) {
                return null;
            }

            const defaultX = Math.max(0, Math.min(window.innerWidth - 384, window.innerWidth - 410 - (index * 24)));
            const defaultY = Math.max(0, Math.min(window.innerHeight - (expanded ? 420 : 160), window.innerHeight - 380 - (index * 24)));
            const pos = cardPositions[transfer.id] || { x: defaultX, y: defaultY };

            const headerColors = [
                'from-blue-600/90 to-indigo-700/90 border-blue-500/30',
                'from-purple-600/90 to-pink-700/90 border-purple-500/30',
                'from-emerald-600/90 to-teal-700/90 border-emerald-500/30',
                'from-amber-600/90 to-orange-600/90 border-amber-500/30',
                'from-rose-600/90 to-red-700/90 border-rose-500/30',
                'from-cyan-600/90 to-blue-700/90 border-cyan-500/30'
            ];
            const idStr = transfer.id || '0';
            const idNum = [...idStr].reduce((acc, char) => acc + char.charCodeAt(0), 0);
            const headerBg = `bg-gradient-to-r ${headerColors[idNum % headerColors.length]}`;

            const filesList = details?.files || [];
            let titleText = "Scanning files...";
            if (filesList.length > 0) {
                if (filesList.length === 1) {
                    titleText = filesList[0].path.split(/[\\/]/).pop();
                } else {
                    titleText = isUpload 
                        ? "the files from local system to remote server" 
                        : "the files from remote server to local system";
                }
            } else if (transfer.current_file) {
                titleText = transfer.current_file.split(/[\\/]/).pop();
            } else {
                titleText = isUpload 
                    ? "the files from local system to remote server" 
                    : "the files from remote server to local system";
            }

            const hasConflicts = filesList.some(f => f.status === 'exists_exact' || f.status === 'exists_partial');
            const hasPartialConflicts = filesList.some(f => f.status === 'exists_partial');

            if (showConfirm) {
                return (
                    <div 
                        class="glass-panel w-96 rounded-xl shadow-2xl p-4 border border-red-500/30 flex flex-col justify-between space-y-4 pointer-events-auto bg-slate-950/95 backdrop-blur-md"
                        style={{
                            position: 'fixed',
                            left: `${pos.x}px`,
                            top: `${pos.y}px`,
                            zIndex: 50
                        }}
                    >
                        <div class="flex items-start space-x-3">
                            <div class="p-2 bg-red-500/10 rounded-lg text-red-500 flex-shrink-0">
                                <svg class="w-6 h-6" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
                                    <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z"/>
                                </svg>
                            </div>
                            <div class="space-y-1">
                                <h4 class="text-sm font-bold text-slate-200">Cancel Active Transfer?</h4>
                                <p class="text-xs text-slate-400">Are you sure you want to stop and cancel the active transfer pipeline? This action cannot be undone.</p>
                            </div>
                        </div>
                        <div class="flex justify-end space-x-2">
                            <button onClick={() => setShowConfirm(false)} class="px-3 py-1.5 rounded-lg border border-slate-700 hover:bg-slate-800 text-slate-300 text-xs font-semibold transition">
                                Keep Transferring
                            </button>
                            <button onClick={() => { setShowConfirm(false); onRemove(transfer.id); }} class="px-3 py-1.5 rounded-lg bg-red-600 hover:bg-red-500 text-white text-xs font-semibold shadow-neon-red transition">
                                Stop & Close
                            </button>
                        </div>
                    </div>
                );
            }

            return (
                <div 
                    class="glass-panel w-96 rounded-xl shadow-2xl overflow-hidden border border-slate-800 flex flex-col pointer-events-auto transition-shadow"
                    style={{
                        position: 'fixed',
                        left: `${pos.x}px`,
                        top: `${pos.y}px`,
                        zIndex: isDragging ? 50 : 40,
                        boxShadow: isDragging ? '0 20px 25px -5px rgb(0 0 0 / 0.5), 0 8px 10px -6px rgb(0 0 0 / 0.5)' : undefined
                    }}
                >
                    {/* Header */}
                    <div 
                        class={`px-4 py-2.5 flex items-center justify-between border-b cursor-grab select-none ${headerBg} ${isDragging ? 'cursor-grabbing' : ''}`}
                        onMouseDown={handleMouseDown}
                    >
                        <div class="flex items-center space-x-2 min-w-0">
                            <span class={`text-[10px] font-extrabold uppercase px-2 py-0.5 rounded shadow-sm flex items-center space-x-0.5 ${isUpload ? 'bg-blue-600 text-white shadow-blue-500/30' : 'bg-emerald-600 text-white shadow-emerald-500/30'}`}>
                                <span>{isUpload ? 'Uploading' : 'Downloading'}</span>
                                {transfer.active && <span class="animate-blink font-mono">...</span>}
                            </span>
                            <span class="text-xs font-semibold text-slate-200 truncate flex items-center" title={titleText}>
                                {titleText}
                                {transfer.active && <span class="animate-blink ml-1">...</span>}
                            </span>
                        </div>
                        <div class="flex items-center space-x-1.5">
                            <button 
                                onClick={handleMinimize} 
                                onMouseDown={e => e.stopPropagation()}
                                class="p-1 hover:bg-slate-850 rounded transition text-slate-400 hover:text-slate-200" 
                                title="Minimize / Collapse"
                            >
                                <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                                    <path strokeLinecap="round" strokeLinejoin="round" d="M18 12H6" />
                                </svg>
                            </button>
                            <button 
                                onClick={(e) => { 
                                    e.stopPropagation(); 
                                    if (transfer.active) {
                                        setShowConfirm(true); 
                                    } else {
                                        const updated = new Set(hiddenCards);
                                        updated.add(transfer.id);
                                        setHiddenCards(updated);
                                    }
                                }} 
                                onMouseDown={e => e.stopPropagation()}
                                class="p-1 hover:bg-slate-850 rounded transition text-slate-400 hover:text-red-400" 
                                title="Close transfer window"
                            >
                                <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                                    <path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12" />
                                </svg>
                            </button>
                        </div>
                    </div>

                    {/* Progress Bar & Summary */}
                    <div class="p-3 space-y-2 flex-shrink-0">
                        <div class="flex justify-between text-xs font-semibold text-slate-300">
                            <span>{formatBytes(transfer.bytes_transferred)} / {formatBytes(transfer.total_bytes)}</span>
                            <span>{percent.toFixed(1)}%</span>
                        </div>
                        <div class="w-full h-2 bg-slate-950 rounded-full overflow-hidden border border-slate-900">
                            <div class={`h-full bg-gradient-to-r ${progressColor} ${neonShadow} rounded-full transition-all duration-300`} style={{ width: `${percent}%` }}></div>
                        </div>
                        {transfer.error && (
                            <div class="text-[11px] text-red-400 font-semibold bg-red-950/20 p-2 rounded border border-red-900/30 break-all">
                                Error: {transfer.error}
                            </div>
                        )}
                        {!transfer.active && !transfer.error && (
                            <div class="text-[11px] text-emerald-400 font-semibold bg-emerald-950/20 p-1.5 rounded border border-emerald-900/30 text-center">
                                Completed successfully
                            </div>
                        )}
                        {!transfer.error && (
                            <div class="flex space-x-2 mt-1">
                                <button onClick={(e) => { e.stopPropagation(); onShowServerStatus(transfer.id); }} class="w-1/2 text-center px-2 py-1 bg-slate-800 hover:bg-slate-755 border border-slate-700 text-slate-300 rounded text-[10px] font-semibold transition" title="Show server session info">
                                    Server Status
                                </button>
                                {transfer.active && (
                                    <button onClick={(e) => { e.stopPropagation(); onControlAll(transfer.id, hasActiveFiles() ? 'pause' : 'resume'); }} class="w-1/2 text-center px-2 py-1 bg-slate-800 hover:bg-slate-755 border border-slate-700 text-slate-300 rounded text-[10px] font-semibold transition" title="Pause or resume all files in this transfer">
                                        {hasActiveFiles() ? 'Pause All' : 'Resume All'}
                                    </button>
                                )}
                            </div>
                        )}
                        {transfer.active && hasConflicts && (
                            <div class="flex flex-col space-y-2 mt-2 bg-amber-950/20 p-1.5 rounded border border-amber-900/30">
                                <div class="flex space-x-2">
                                    <button onClick={(e) => { e.stopPropagation(); onControlAll(transfer.id, 'overwrite_all'); }} class="w-full text-center py-1 bg-amber-700 hover:bg-amber-600 text-white rounded text-[10px] font-bold transition">
                                        Overwrite All
                                    </button>
                                    {hasPartialConflicts && (
                                        <button onClick={(e) => { e.stopPropagation(); onControlAll(transfer.id, 'overwrite_resume_all'); }} class="w-full text-center py-1 bg-emerald-700 hover:bg-emerald-600 text-white rounded text-[10px] font-bold transition">
                                            Overwrite & Resume All
                                        </button>
                                    )}
                                </div>
                                <div class="flex space-x-2">
                                    <button onClick={(e) => { e.stopPropagation(); onControlAll(transfer.id, 'delta_sync_all'); }} class="w-full text-center py-1 bg-indigo-700 hover:bg-indigo-600 text-white rounded text-[10px] font-bold transition">
                                        Delta Sync All
                                    </button>
                                    {hasPartialConflicts && (
                                        <button onClick={(e) => { e.stopPropagation(); onControlAll(transfer.id, 'delta_sync_resume_all'); }} class="w-full text-center py-1 bg-indigo-700 hover:bg-indigo-600 text-white rounded text-[10px] font-bold transition">
                                            Delta Sync & Resume All
                                        </button>
                                    )}
                                </div>
                            </div>
                        )}
                    </div>
)rawhtml";
    html += R"rawhtml(
                    {/* Expandable Files List */}
                    {expanded && (
                        <div class="flex-1 overflow-y-auto border-t border-slate-850 bg-slate-950/40 divide-y divide-slate-850/60 p-2 max-h-48">
                            {details && details.files && details.files.length > 0 ? (
                                details.files.map((file, idx) => {
                                    const filePercent = file.total_bytes > 0 ? (file.bytes_transferred / file.total_bytes) * 100 : 0;
                                    const name = file.path.split(/[\\/]/).pop();
                                    
                                    return (
                                        <div key={idx} class="py-1.5 px-1 flex flex-col space-y-1 text-xs">
                                            <div class="flex justify-between items-center min-w-0 gap-2">
                                                <span class="text-slate-300 font-medium truncate flex-1 font-mono text-[11px]" title={file.path}>
                                                    {name}
                                                </span>
                                                <span class="text-[10px] text-slate-400 font-mono flex-shrink-0">
                                                    {file.status === 'completed' && <span class="text-emerald-400">✓ Done</span>}
                                                    {file.status === 'skipped' && <span class="text-amber-400">Skipped</span>}
                                                    {file.status === 'failed' && <span class="text-red-400">Failed</span>}
                                                    {file.status === 'paused' && <span class="text-cyan-400 animate-pulse">Paused</span>}
                                                    {file.status === 'transferring' && <span class="text-blue-400 animate-pulse font-bold">Active</span>}
                                                    {file.status === 'pending' && file.decision === 'none' && <span class="text-slate-500">Pending</span>}
                                                    {file.status === 'pending' && file.decision !== 'none' && <span class="text-slate-400 animate-pulse">Ready ({file.decision})</span>}
                                                    {file.status === 'exists_exact' && <span class="text-amber-400 font-semibold">File Exists</span>}
                                                    {file.status === 'exists_partial' && <span class="text-cyan-400 font-semibold">Conflict (Partial)</span>}
                                                </span>
                                            </div>

                                            <div class="flex justify-between items-center text-[10px] text-slate-400 font-mono">
                                                <span>{formatBytes(file.bytes_transferred)} / {formatBytes(file.total_bytes)} {file.rate_string ? `(${file.rate_string})` : ''}</span>
                                                <span>{filePercent.toFixed(0)}%</span>
                                            </div>

                                            {/* File actions */}
                                            {transfer.active && (
                                                <div class="flex items-center space-x-2 pt-1">
                                                    {file.status === 'pending' && file.decision === 'none' && (
                                                        <button onClick={(e) => handleFileControl(e, file.path, 'start')} class="px-2 py-0.5 bg-blue-600 hover:bg-blue-500 text-white rounded text-[10px] font-semibold transition flex items-center">
                                                            <PlayIcon /> Start
                                                        </button>
                                                    )}
                                                    {file.status === 'exists_exact' && (
                                                        <React.Fragment>
                                                            <button onClick={(e) => handleFileControl(e, file.path, 'overwrite')} class="px-2 py-0.5 bg-amber-600 hover:bg-amber-500 text-white rounded text-[10px] font-semibold transition flex items-center">
                                                                Overwrite
                                                            </button>
                                                            <button onClick={(e) => handleFileControl(e, file.path, 'delta_sync')} class="px-2 py-0.5 bg-indigo-600 hover:bg-indigo-500 text-white rounded text-[10px] font-semibold transition flex items-center">
                                                                Delta Sync
                                                            </button>
                                                        </React.Fragment>
                                                    )}
                                                    {file.status === 'exists_partial' && (
                                                        <React.Fragment>
                                                            <button onClick={(e) => handleFileControl(e, file.path, 'resume')} class="px-2 py-0.5 bg-emerald-600 hover:bg-emerald-500 text-white rounded text-[10px] font-semibold transition flex items-center">
                                                                <PlayIcon /> Resume
                                                            </button>
                                                            <button onClick={(e) => handleFileControl(e, file.path, 're-transfer')} class="px-2 py-0.5 bg-amber-600 hover:bg-amber-500 text-white rounded text-[10px] font-semibold transition flex items-center">
                                                                Re-transfer
                                                            </button>
                                                            <button onClick={(e) => handleFileControl(e, file.path, 'delta_sync')} class="px-2 py-0.5 bg-indigo-600 hover:bg-indigo-500 text-white rounded text-[10px] font-semibold transition flex items-center">
                                                                Delta Sync
                                                            </button>
                                                        </React.Fragment>
                                                    )}
                                                    {file.status === 'transferring' && (
                                                        <button onClick={(e) => handleFileControl(e, file.path, 'pause')} class="px-2 py-0.5 bg-slate-800 hover:bg-slate-755 text-slate-350 rounded text-[10px] font-semibold border border-slate-700 transition flex items-center">
                                                            <PauseIcon /> Pause
                                                        </button>
                                                    )}
                                                    {file.status === 'paused' && (
                                                        <button onClick={(e) => handleFileControl(e, file.path, 'resume')} class="px-2 py-0.5 bg-cyan-600 hover:bg-cyan-500 text-white rounded text-[10px] font-semibold transition flex items-center">
                                                            <PlayIcon /> Resume
                                                        </button>
                                                    )}
                                                    {(file.status === 'pending' || file.status === 'exists_exact' || file.status === 'exists_partial' || file.status === 'transferring' || file.status === 'paused') && (
                                                        <button onClick={(e) => handleFileControl(e, file.path, 'skip')} class="px-2 py-0.5 bg-red-950/40 hover:bg-red-900/40 text-red-400 rounded text-[10px] font-semibold border border-red-900/30 transition flex items-center">
                                                            <SkipIcon /> Skip
                                                        </button>
                                                    )}
                                                </div>
                                            )}
                                        </div>
                                    );
                                })
                            ) : (
                                <div class="text-[11px] text-slate-500 text-center py-4">
                                    {transfer.active ? "Initializing transfer stream..." : "No file transfer logs."}
                                </div>
                            )}
                        </div>
                    )}
                </div>
            );
        };

        function App() {
            // Local state
            const [localDrives, setLocalDrives] = useState([]);
            const [localPath, setLocalPath] = useState("C:\\");
            const [localInput, setLocalInput] = useState("C:\\");
            const [localFiles, setLocalFiles] = useState([]);
            const [localSelected, setLocalSelected] = useState(new Set());
            const [localSort, setLocalSort] = useState({ col: 'name', asc: true });
            
            // Connection & Remote State
            const [profiles, setProfiles] = useState([]);
            const [remoteConnected, setRemoteConnected] = useState(false);
            const [remoteHost, setRemoteHost] = useState("");
            const [remotePort, setRemotePort] = useState("1245");
            const [remoteUsername, setRemoteUsername] = useState("");
            const [remoteAuthMethod, setRemoteAuthMethod] = useState("none"); // none, password, mlkem
            const [remotePassword, setRemotePassword] = useState("");
            const [remoteKeyFile, setRemoteKeyFile] = useState("");
            const [remoteKeyPass, setRemoteKeyPass] = useState("");
            const [remoteSecretKey, setRemoteSecretKey] = useState("0xebf5fa7d3e9fcf67e874baddee773b1d5badd6eb75baddee974b2592c9643299");
            const [remoteSecurityLevel, setRemoteSecurityLevel] = useState("high"); // high, fast, aes, AES-256-GCM
            const [showConnectModal, setShowConnectModal] = useState(false);
            
            const [remoteAllowedPaths, setRemoteAllowedPaths] = useState([]);
            const [remoteAllowedPathsInput, setRemoteAllowedPathsInput] = useState("D:\\src, D:\\Work\\FILES");
            const [remotePath, setRemotePath] = useState("/");
            const [remoteInput, setRemoteInput] = useState("/");
            const [remoteFiles, setRemoteFiles] = useState([]);
            const [remoteSelected, setRemoteSelected] = useState(new Set());
            const [remoteSort, setRemoteSort] = useState({ col: 'name', asc: true });

            // Global settings
            const [resumeTransfer, setResumeTransfer] = useState(false);
            const [forceOverwrite, setForceOverwrite] = useState(true);

            // Active transfer state
            const [transfers, setTransfers] = useState([]);
            const [expandedCards, setExpandedCards] = useState(new Set());

            // History & Card Placement States
            const [hiddenCards, setHiddenCards] = useState(() => new Set());
            const [cardPositions, setCardPositions] = useState({});
            const [showHistoryDrawer, setShowHistoryDrawer] = useState(false);

            const isPathAllowed = (pathToCheck) => {
                if (!remoteConnected) return true;
                if (!remoteAllowedPaths || remoteAllowedPaths.length === 0) return true;
                
                const normCheck = pathToCheck.replace(/[\\/]+/g, '\\').toLowerCase();
                return remoteAllowedPaths.some(allowed => {
                    const normAllowed = allowed.replace(/[\\/]+/g, '\\').toLowerCase();
                    return normCheck === normAllowed || normCheck.startsWith(normAllowed + '\\');
                });
            };

            const changeRemotePath = (newPath) => {
                if (isPathAllowed(newPath)) {
                    setRemotePath(newPath);
                } else {
                    addLog(`Navigation blocked: Path "${newPath}" is not within the server's allowed root paths.`, "WARNING");
                }
            };
            
            // Console audit logs
            const [logs, setLogs] = useState([]);
            const logEndRef = useRef(null);

            // Server Session Modal state
            const [showSessionModal, setShowSessionModal] = useState(false);
            const [sessionTransferId, setSessionTransferId] = useState("");
            const [sessionInfo, setSessionInfo] = useState(null);
            const [loadingSession, setLoadingSession] = useState(false);
            const [sessionError, setSessionError] = useState("");

            const [createFolderConfig, setCreateFolderConfig] = useState({ show: false, type: 'local', parentPath: '', folderName: '' });

            // Share Modal state
            const [shareConfig, setShareConfig] = useState({ show: false, fileName: '', filePath: '', shareUrl: '', expirySeconds: 3600, maxDownloads: 0 });

            // Fetch local drives on load
            useEffect(() => {
                fetchDrives();
                loadProfiles();
                addLog("NetCopy GUI client started.");
            }, []);

            // Log autoscroll
            useEffect(() => {
                logEndRef.current?.scrollIntoView({ behavior: 'smooth' });
            }, [logs]);

            const refreshExplorers = () => {
                if (localPath) fetchLocalFiles(localPath);
                if (remoteConnected && remotePath) fetchRemoteFiles(remotePath);
            };

            const removeTransfer = async (id) => {
                if (!id) return;
                try {
                    addLog(`Stopping and removing transfer pipeline ${id}...`, "WARNING");
                    const res = await fetch('/api/transfer/remove', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ id })
                    });
                    if (res.ok) {
                        setTransfers(prev => prev.filter(t => t.id !== id));
                        addLog(`Transfer pipeline ${id} successfully stopped and removed.`);
                    }
                } catch (err) {
                    addLog("Failed to remove transfer: " + err.message, "ERROR");
                }
            };

            const showServerStatus = (transferId) => {
                setSessionTransferId(transferId);
                setShowSessionModal(true);
            };

            const controlAllFiles = async (transferId, action) => {
                try {
                    addLog(`Sending ${action} request for all files in transfer ${transferId}...`);
                    const res = await fetch('/api/transfer/control', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ id: transferId, action })
                    });
                    if (res.ok) {
                        addLog(`Successfully requested ${action} for all files in transfer ${transferId}.`);
                    }
                } catch (err) {
                    addLog(`Failed to control transfer ${transferId}: ` + err.message, "ERROR");
                }
            };
)rawhtml";
    html += R"rawhtml(
            const triggerCreateFolder = (type) => {
                const parentPath = type === 'local' ? localPath : remotePath;
                setCreateFolderConfig({
                    show: true,
                    type,
                    parentPath,
                    folderName: ''
                });
            };

            const createNewFolder = async (type, parentPath, name) => {
                if (!name) return;
                const endpoint = type === 'local' ? '/api/local/create_dir' : '/api/remote/create_dir';
                try {
                    const res = await fetch(`${endpoint}?path=${encodeURIComponent(parentPath)}&name=${encodeURIComponent(name)}`, {
                        method: 'POST'
                    });
                    const data = await res.json();
                    if (data.success) {
                        addLog(`Created ${type} folder: ${name}`);
                        refreshExplorers();
                    } else {
                        addLog(`Failed to create ${type} folder: ${data.error}`, "ERROR");
                        alert(`Failed to create folder: ${data.error}`);
                    }
                } catch (err) {
                    addLog(`Error creating ${type} folder: ${err.message}`, "ERROR");
                    alert(`Error creating folder: ${err.message}`);
                }
            };

            const triggerShareLocal = (fileName) => {
                let sep = localPath.includes('/') ? '/' : '\\';
                let filePath = localPath;
                if (!filePath.endsWith('/') && !filePath.endsWith('\\')) {
                    filePath += sep;
                }
                filePath += fileName;
                setShareConfig({
                    show: true,
                    fileName,
                    filePath,
                    shareUrl: '',
                    expirySeconds: 3600,
                    maxDownloads: 0
                });
            };

            const generateShareLink = async () => {
                try {
                    const res = await fetch('/api/share/create', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json'
                        },
                        body: JSON.stringify({
                            file_path: shareConfig.filePath,
                            expiry_seconds: Number(shareConfig.expirySeconds),
                            max_downloads: Number(shareConfig.maxDownloads)
                        })
                    });
                    const data = await res.json();
                    if (data.status === 'success') {
                        setShareConfig(prev => ({ ...prev, shareUrl: data.share_url }));
                        addLog(`Generated share link for: ${shareConfig.fileName}`);
                    } else {
                        addLog(`Failed to generate share link: ${data.error}`, "ERROR");
                    }
                } catch (err) {
                    addLog(`Error generating share link: ${err.message}`, "ERROR");
                }
            };

            // Remote Connection Lost Heartbeat check
            useEffect(() => {
                if (!remoteConnected) return;

                const checkStatus = async () => {
                    try {
                        const res = await fetch('/api/remote/check');
                        if (res.ok) {
                            const data = await res.json();
                            if (!data.connected) {
                                handleServerDisconnection();
                            }
                        } else {
                            handleServerDisconnection();
                        }
                    } catch (err) {
                        handleServerDisconnection();
                    }
                };

                const handleServerDisconnection = () => {
                    setRemoteConnected(false);
                    addLog("Remote server connection lost or disconnected.", "ERROR");
                    // Pause all active transfers on connection drop
                    transfers.forEach(t => {
                        if (t.active) {
                            controlAllFiles(t.id, 'pause');
                        }
                    });
                };

                const interval = setInterval(checkStatus, 3000);
                return () => clearInterval(interval);
            }, [remoteConnected, transfers]);

            // Server Session Details auto-polling
            useEffect(() => {
                let intervalId;
                const fetchSession = async () => {
                    if (!sessionTransferId) return;
                    try {
                        const res = await fetch(`/api/transfer/server_session?id=${encodeURIComponent(sessionTransferId)}`);
                        if (res.ok) {
                            const data = await res.json();
                            if (data.success) {
                                setSessionInfo(data);
                                setSessionError("");
                            } else {
                                setSessionError(data.error);
                            }
                        }
                    } catch (err) {
                        setSessionError("Failed to fetch session: " + err.message);
                    }
                };

                if (showSessionModal && sessionTransferId) {
                    setLoadingSession(true);
                    fetchSession().then(() => setLoadingSession(false));
                    intervalId = setInterval(fetchSession, 2000);
                } else {
                    setSessionInfo(null);
                    setSessionError("");
                }

                return () => {
                    if (intervalId) clearInterval(intervalId);
                };
            }, [showSessionModal, sessionTransferId]);

            // Periodic polling for active transfers
            useEffect(() => {
                let activeTimer = 0;
                const fetchTransfers = async () => {
                    try {
                        const res = await fetch('/api/transfers');
                        const data = await res.json();
                        setTransfers(prevTransfers => {
                            if (prevTransfers && prevTransfers.length > 0 && data) {
                                prevTransfers.forEach(prevT => {
                                    const nextT = data.find(t => t.id === prevT.id);
                                    if (prevT.active && nextT && !nextT.active) {
                                        setTimeout(() => {
                                            refreshExplorers();
                                        }, 500);
                                    }
                                });
                            }
                            return data || [];
                        });

                        // Auto-refresh every 3s if there is any active transfer
                        const hasActive = data && data.some(t => t.active);
                        if (hasActive) {
                            activeTimer++;
                            if (activeTimer >= 3) {
                                activeTimer = 0;
                                refreshExplorers();
                            }
                        } else {
                            activeTimer = 0;
                        }
                    } catch (err) {
                        console.error(err);
                    }
                };
                fetchTransfers();
                const interval = setInterval(fetchTransfers, 1000);
                return () => clearInterval(interval);
            }, [localPath, remotePath, remoteConnected]);

            // Poll list files when paths change
            useEffect(() => {
                if (localPath) fetchLocalFiles(localPath);
            }, [localPath]);

            useEffect(() => {
                if (remoteConnected && remotePath) fetchRemoteFiles(remotePath);
            }, [remoteConnected, remotePath]);

            // Sync user inputs when path states change
            useEffect(() => {
                setLocalInput(localPath);
            }, [localPath]);

            useEffect(() => {
                setRemoteInput(remotePath);
            }, [remotePath]);

            const addLog = (msg, level = 'INFO') => {
                const time = new Date().toLocaleTimeString();
                setLogs(prev => [...prev, `[${time}] [${level}] ${msg}`]);
            };

            const fetchDrives = async () => {
                try {
                    const res = await fetch('/api/drives');
                    const data = await res.json();
                    setLocalDrives(data.drives || []);
                    if (data.drives && data.drives.length > 0 && !localPath) {
                        setLocalPath(data.drives[0]);
                    }
                } catch (err) {
                    addLog("Failed to fetch local drives: " + err.message, "ERROR");
                }
            };
)rawhtml";
    html += R"rawhtml(            const loadProfiles = async () => {
                try {
                    const res = await fetch('/api/profiles');
                    const parsed = await res.json();
                    if (Array.isArray(parsed) && parsed.length > 0) {
                        setProfiles(parsed);
                        applyProfile(parsed[0]);
                    } else {
                        const stored = localStorage.getItem('netcopy_profiles');
                        if (stored) {
                            const parsedStored = JSON.parse(stored);
                            setProfiles(parsedStored);
                            if (parsedStored.length > 0) {
                                applyProfile(parsedStored[0]);
                            }
                        }
                    }
                } catch (e) {
                    console.error(e);
                }
            };

            const saveProfile = async () => {
                if (!remoteHost) return;
                const profileName = prompt("Enter a name for this profile:", remoteHost) || remoteHost;
                const newProfile = {
                    name: profileName,
                    host: remoteHost,
                    port: remotePort,
                    username: remoteUsername,
                    auth_method: remoteAuthMethod,
                    password: remotePassword,
                    private_key_file: remoteKeyFile,
                    private_key_passphrase: remoteKeyPass,
                    secret_key: remoteSecretKey,
                    security_level: remoteSecurityLevel,
                    allowed_paths_input: remoteAllowedPathsInput
                };

                try {
                    const res = await fetch('/api/profiles/save', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(newProfile)
                    });
                    const data = await res.json();
                    if (data.success) {
                        addLog(`Saved profile to client.conf: ${profileName}`);
                        await loadProfiles();
                    } else {
                        addLog(`Failed to save profile: ${data.error}`, "ERROR");
                    }
                } catch (err) {
                    addLog(`Error saving profile: ${err.message}`, "ERROR");
                }
            };

            const deleteProfile = async (profileName) => {
                if (!confirm(`Are you sure you want to delete profile "${profileName}"?`)) return;
                try {
                    const res = await fetch('/api/profiles/delete', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ name: profileName })
                    });
                    const data = await res.json();
                    if (data.success) {
                        addLog(`Deleted profile: ${profileName}`);
                        await loadProfiles();
                    } else {
                        addLog(`Failed to delete profile: ${data.error}`, "ERROR");
                    }
                } catch (err) {
                    addLog(`Error deleting profile: ${err.message}`, "ERROR");
                }
            };

            const applyProfile = (p) => {
                setRemoteHost(p.host || "");
                setRemotePort(p.port || "1245");
                setRemoteUsername(p.username || "");
                setRemoteAuthMethod(p.auth_method || "none");
                setRemotePassword(p.password || "");
                setRemoteKeyFile(p.private_key_file || "");
                setRemoteKeyPass(p.private_key_passphrase || "");
                setRemoteSecretKey(p.secret_key || "");
                setRemoteSecurityLevel(p.security_level || "high");
                setRemoteAllowedPathsInput(p.allowed_paths_input || "D:\\src, D:\\Work\\FILES");
            };

            const fetchLocalFiles = async (path) => {
                try {
                    const res = await fetch(`/api/local/list?path=${encodeURIComponent(path)}`);
                    const data = await res.json();
                    if (data.error) {
                        addLog(data.error, "ERROR");
                        if (path !== "C:\\" && path !== "C:/") {
                            setLocalPath("C:\\");
                        }
                    } else {
                        setLocalFiles(data.files || []);
                        setLocalSelected(new Set());
                    }
                } catch (err) {
                    addLog("Failed to list local path: " + err.message, "ERROR");
                }
            };

            const fetchRemoteFiles = async (path) => {
                try {
                    const res = await fetch(`/api/remote/list?path=${encodeURIComponent(path)}`);
                    const data = await res.json();
                    if (data.error) {
                        addLog("Remote Error: " + data.error, "ERROR");
                    } else {
                        setRemoteFiles(data.files || []);
                        setRemoteSelected(new Set());
                    }
                } catch (err) {
                    addLog("Failed to list remote path: " + err.message, "ERROR");
                }
            };

            const handleConnect = async (e) => {
                e.preventDefault();
                addLog(`Connecting to remote host ${remoteHost}:${remotePort}...`);
                try {
                    const res = await fetch('/api/connect', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({
                            host: remoteHost,
                            port: parseInt(remotePort),
                            username: remoteUsername,
                            auth_method: remoteAuthMethod,
                            password: remotePassword,
                            private_key_file: remoteKeyFile,
)rawhtml";
    html += R"rawhtml(                            private_key_passphrase: remoteKeyPass,
                            secret_key: remoteSecretKey,
                            security_level: remoteSecurityLevel,
                            allowed_paths: remoteAllowedPathsInput
                        })
                    });
                    const data = await res.json();
                    if (data.success) {
                        setRemoteConnected(true);
                        setRemoteAllowedPaths(data.allowed_paths || []);
                        changeRemotePath(data.allowed_paths?.[0] || "/");
                        addLog(`Successfully connected to ${remoteHost}:${remotePort}! Allowed root: ${data.allowed_paths?.[0] || '/'}`);
                        setShowConnectModal(false);
                    } else {
                        addLog("Connection failed: " + data.error, "ERROR");
                        alert("Connection failed: " + data.error);
                    }
                } catch (err) {
                    addLog("Failed to connect: " + err.message, "ERROR");
                    alert("Failed to connect: " + err.message);
                }
            };

            const handleDisconnect = async () => {
                try {
                    await fetch('/api/disconnect', { method: 'POST' });
                } catch (err) {
                    console.error("Error disconnecting:", err);
                }
                setRemoteConnected(false);
                addLog("Disconnected from remote server.");
            };

            const startTransfer = async (direction) => {
                const isUpload = direction === 'upload';
                const selectedSet = isUpload ? localSelected : remoteSelected;
                if (selectedSet.size === 0) {
                    alert(`Please select files to ${direction} first.`);
                    return;
                }

                if (!remoteConnected) {
                    alert("Please connect to a remote server first.");
                    return;
                }

                const items = Array.from(selectedSet);
                addLog(`Initiating ${direction} of ${items.length} items...`);
                
                try {
                    const res = await fetch('/api/transfer', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({
                            direction,
                            local_path: localPath,
                            remote_path: remotePath,
                            items,
                            resume: resumeTransfer,
                            force: forceOverwrite
                        })
                    });
                    const data = await res.json();
                    if (data.success) {
                        addLog(`Transfer pipeline ${data.id} initialized.`);
                    } else {
                        addLog("Failed to start transfer: " + data.error, "ERROR");
                    }
                } catch (err) {
                    addLog("Transfer error: " + err.message, "ERROR");
                }
            };
)rawhtml";
    html += R"rawhtml(            // Local Directory navigation
            const handleLocalDoubleClick = (file) => {
                if (file.is_dir) {
                    if (file.name === '..') {
                        handleLocalBack();
                        return;
                    }
                    let newPath = localPath;
                    if (!newPath.endsWith('\\') && !newPath.endsWith('/')) {
                        newPath += '\\';
                    }
                    newPath += file.name;
                    setLocalPath(newPath);
                }
            };

            const handleLocalBack = () => {
                let parts = localPath.split(/[\\/]/).filter(p => p.length > 0);
                if (parts.length <= 1) {
                    return;
                }
                parts.pop();
                let parent = parts.join('\\');
                if (parent.endsWith(':')) parent += '\\';
                setLocalPath(parent);
            };

            const isWindowsPath = (path) => {
                if (!path) return false;
                return /^[a-zA-Z]:/.test(path) || path.includes('\\');
            };

            // Remote Directory navigation
            const handleRemoteDoubleClick = (file) => {
                if (file.is_dir) {
                    if (file.name === '..') {
                        handleRemoteBack();
                        return;
                    }
                    if (!file.name || file.name === '\\' || file.name === '/') {
                        return;
                    }
                    let newPath = remotePath;
                    const isWin = isWindowsPath(newPath);
                    const sep = isWin ? '\\' : '/';
                    if (!newPath.endsWith('\\') && !newPath.endsWith('/')) {
                        newPath += sep;
                    }
                    newPath += file.name;
                    changeRemotePath(newPath);
                }
            };

            const handleRemoteBack = () => {
                const isWin = isWindowsPath(remotePath);
                if (isWin) {
                    let parts = remotePath.split(/[\\/]/).filter(p => p.length > 0);
                    if (parts.length <= 1) {
                        return;
                    }
                    parts.pop();
                    let parent = parts.join('\\');
                    if (parent.endsWith(':')) parent += '\\';
                    changeRemotePath(parent);
                } else {
                    if (remotePath === "/" || remotePath === "\\") return;
                    let parts = remotePath.split(/[\\/]/).filter(p => p.length > 0);
                    if (parts.length === 0) return;
                    parts.pop();
                    changeRemotePath(parts.length > 0 ? "/" + parts.join('/') : "/");
                }
            };

            // Table Selection Helpers
            const toggleSelectLocal = (name) => {
                const updated = new Set(localSelected);
                if (updated.has(name)) updated.delete(name);
                else updated.add(name);
                setLocalSelected(updated);
            };

            const toggleSelectRemote = (name) => {
                const updated = new Set(remoteSelected);
                if (updated.has(name)) updated.delete(name);
                else updated.add(name);
                setRemoteSelected(updated);
            };

            // Sorting logic
            const getSortedFiles = (files, sortConfig) => {
                return [...files].sort((a, b) => {
                    if (a.is_dir !== b.is_dir) {
                        return a.is_dir ? -1 : 1;
                    }
                    let valA = a[sortConfig.col];
                    let valB = b[sortConfig.col];
                    if (sortConfig.col === 'size') {
                        valA = parseInt(valA) || 0;
                        valB = parseInt(valB) || 0;
                    }
                    if (valA < valB) return sortConfig.asc ? -1 : 1;
                    if (valA > valB) return sortConfig.asc ? 1 : -1;
                    return 0;
                });
            };

            const localSortedFiles = getSortedFiles(localFiles, localSort);
            const remoteSortedFiles = getSortedFiles(remoteFiles, remoteSort);

            return (
                <React.Fragment>
                    {/* Top Status Bar */}
                    <div class="glass-panel rounded-xl p-4 flex flex-wrap items-center justify-between gap-4">
                        <div class="flex items-center space-x-3">
                            <div class="h-3 w-3 rounded-full bg-blue-500 shadow-neon-blue animate-pulse"></div>
                            <span class="font-semibold text-lg tracking-wider text-transparent bg-clip-text bg-gradient-to-r from-blue-400 to-indigo-300">NETCOPY WORKSPACE v0.81</span>
                        </div>
                        
                        <div class="flex items-center space-x-4">
                            <button onClick={() => setShowHistoryDrawer(true)} class="px-3.5 py-1.5 rounded-lg border border-slate-750 bg-slate-900/60 hover:bg-slate-800 text-slate-350 text-sm font-semibold shadow transition flex items-center space-x-1.5" title="View transfer session history">
                                <svg className="w-4 h-4 text-indigo-400" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
                                    <path strokeLinecap="round" strokeLinejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
                                </svg>
                                <span>History</span>
                            </button>

                            {remoteConnected ? (
                                <div class="flex items-center space-x-3">
                                    <span class="text-sm text-emerald-400 font-medium">Connected: {remoteUsername || 'anonymous'}@{remoteHost}</span>
                                    <button onClick={handleDisconnect} class="px-3 py-1.5 rounded-lg border border-red-500/50 hover:bg-red-500/10 text-red-400 text-sm font-semibold transition">Disconnect</button>
                                </div>
                            ) : (
                                <button onClick={() => setShowConnectModal(true)} class="px-4 py-2 rounded-lg bg-gradient-to-r from-blue-600 to-indigo-600 hover:from-blue-500 hover:to-indigo-500 text-white text-sm font-semibold shadow-neon-blue transition">Connect Server</button>
                            )}
                        </div>
                    </div>
)rawhtml";
    html += R"rawhtml(                    {/* Dual Panels Layout */}
                    <div class="flex-1 grid grid-cols-1 md:grid-cols-2 gap-4 min-h-0">
                        
                        {/* Left Pane - Local Directory Explorer */}
                        <div class="glass-panel rounded-xl flex flex-col min-h-0 p-4">
                            <div class="flex items-center justify-between mb-3 gap-2">
                                <div class="flex items-center space-x-2">
                                    <span class="font-bold text-slate-200">Local Filesystem</span>
                                    <select value={localPath.substring(0, 3)} onChange={e => setLocalPath(e.target.value)} class="glass-input rounded-lg text-xs py-1 px-2 text-slate-300 font-mono">
                                        {localDrives.map(d => <option key={d} value={d}>{d}</option>)}
                                    </select>
                                </div>
                                <div class="flex items-center space-x-2">
                                    <button onClick={handleLocalBack} class="p-1.5 rounded-lg border border-slate-700 hover:bg-slate-800 transition" title="Up one folder">
                                        <ArrowUpIcon />
                                    </button>
                                    <button onClick={() => triggerCreateFolder('local')} class="p-1.5 rounded-lg border border-slate-700 hover:bg-slate-850 transition text-emerald-400 hover:text-emerald-300" title="Create New Folder">
                                        <svg class="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                                            <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v6m3-3H9m12 0a9 9 0 11-18 0 9 9 0 0118 0z" />
                                        </svg>
                                    </button>
                                    <button onClick={() => fetchLocalFiles(localPath)} class="p-1.5 rounded-lg border border-slate-700 hover:bg-slate-800 transition" title="Refresh">
                                        <RefreshIcon />
                                    </button>
                                </div>
                            </div>
                            
                            {/* Local Path Navigation Bar */}
                            <div class="mb-4">
                                <input type="text" value={localInput} onChange={e => setLocalInput(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') setLocalPath(localInput); }} onBlur={() => setLocalPath(localInput)} class="w-full glass-input rounded-lg py-1.5 px-3 text-sm font-mono text-slate-300 focus:ring-1 focus:ring-blue-500" />
                            </div>

                            {/* Local Directory Table */}
                            <div class="flex-1 overflow-auto rounded-lg border border-slate-800 bg-slate-950/40">
                                <table class="w-full text-sm text-left text-slate-300">
                                            <thead class="sticky top-0 bg-slate-900 text-xs text-slate-400 uppercase tracking-wider border-b border-slate-800">
                                                <tr>
                                                    <th class="p-3 w-8"><input type="checkbox" checked={localSortedFiles.length > 0 && localSelected.size === localSortedFiles.length} onChange={e => {
                                                        if (e.target.checked) setLocalSelected(new Set(localSortedFiles.map(f => f.name)));
                                                        else setLocalSelected(new Set());
                                                    }} class="rounded bg-slate-900 border-slate-700 text-blue-500 focus:ring-blue-500"/></th>
                                                    <th class="p-3 cursor-pointer" onClick={() => setLocalSort({ col: 'name', asc: !localSort.asc })}>Name</th>
                                                    <th class="p-3 cursor-pointer w-20" onClick={() => setLocalSort({ col: 'size', asc: !localSort.asc })}>Size</th>
                                                    <th class="p-3 cursor-pointer w-32" onClick={() => setLocalSort({ col: 'last_modified', asc: !localSort.asc })}>Modified</th>
                                                    <th class="p-3 w-16 text-right">Actions</th>
                                                </tr>
                                            </thead>
                                            <tbody class="divide-y divide-slate-850">
                                                {localSortedFiles.map(file => (
                                                    <tr key={file.name} onDoubleClick={() => handleLocalDoubleClick(file)} class={`hover:bg-slate-800/40 transition cursor-pointer select-none ${localSelected.has(file.name) ? 'bg-blue-500/10' : ''}`} onClick={() => toggleSelectLocal(file.name)}>
                                                        <td class="p-3" onClick={e => e.stopPropagation()}>
                                                            <input type="checkbox" checked={localSelected.has(file.name)} onChange={() => toggleSelectLocal(file.name)} class="rounded bg-slate-900 border-slate-700 text-blue-500 focus:ring-blue-500"/>
                                                        </td>
                                                        <td class="p-3 flex items-center font-medium max-w-[200px] truncate">
                                                            {file.is_dir ? <FolderIcon /> : <FileIcon />}
                                                            <span class="truncate">{file.name}</span>
                                                        </td>
                                                        <td class="p-3 text-slate-400 font-mono text-xs">{file.is_dir ? 'Folder' : formatBytes(file.size)}</td>
                                                        <td class="p-3 text-slate-400 text-xs">{formatDate(file.last_modified)}</td>
                                                        <td class="p-3 text-right" onClick={e => e.stopPropagation()}>
                                                            {!file.is_dir && (
                                                                <button onClick={() => triggerShareLocal(file.name)} class="p-1.5 rounded-lg border border-slate-700 hover:bg-slate-800 transition text-blue-400 hover:text-blue-300" title="Share file">
                                                                    <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                                                                        <path strokeLinecap="round" strokeLinejoin="round" d="M7.217 10.907a2.25 2.25 0 100 2.186m0-2.186l.932-.518a5.003 5.003 0 013.07-1.877m-4.002 2.395a4.993 4.993 0 000 2.186m0 0l.932.518a5.003 5.003 0 003.07 1.877m-3.07-1.877a2.25 2.25 0 114.12 1.323 2.25 2.25 0 01-4.12-1.323zm0-3.39a2.25 2.25 0 114.12-1.322 2.25 2.25 0 01-4.12 1.322z" />
                                                                    </svg>
                                                                </button>
                                                            )}
                                                        </td>
                                                    </tr>
                                                ))}
                                            </tbody>
                                </table>
                            </div>
                            
                            {/* Local selection summary & action */}
                            <div class="mt-3 flex items-center justify-between text-xs text-slate-400">
                                <span>{localSelected.size} of {localFiles.length} items selected</span>
                                <button onClick={() => startTransfer('upload')} class="px-4 py-2 rounded-lg bg-blue-600 hover:bg-blue-500 text-white font-semibold flex items-center space-x-1 shadow-neon-blue transition">
                                    <span>Upload to Server</span>
                                    <svg class="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M13 5l7 7-7 7M5 5l7 7-7 7"></path></svg>
                                </button>
                            </div>
                        </div>

                        {/* Right Pane - Remote Server Explorer */}
                        <div class="glass-panel rounded-xl flex flex-col min-h-0 p-4">
                            {remoteConnected ? (
                                <React.Fragment>
                                    <div class="flex items-center justify-between mb-3 gap-2">
                                        <div class="flex items-center space-x-2">
                                            <span class="font-bold text-slate-200">Remote Server</span>
                                            {remoteAllowedPaths.length > 0 && (
                                                <select value={remotePath} onChange={e => changeRemotePath(e.target.value)} class="glass-input rounded-lg text-xs py-1 px-2 text-slate-300 font-mono">
                                                    {remoteAllowedPaths.map(p => <option key={p} value={p}>{p}</option>)}
                                                </select>
                                            )}
                                        </div>
                                        <div class="flex items-center space-x-2">
                                            <button onClick={handleRemoteBack} class="p-1.5 rounded-lg border border-slate-700 hover:bg-slate-800 transition" title="Up one folder">
                                                <ArrowUpIcon />
                                            </button>
                                            <button onClick={() => triggerCreateFolder('remote')} class="p-1.5 rounded-lg border border-slate-700 hover:bg-slate-850 transition text-emerald-400 hover:text-emerald-300" title="Create New Folder">
                                                <svg class="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                                                    <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v6m3-3H9m12 0a9 9 0 11-18 0 9 9 0 0118 0z" />
                                                </svg>
                                            </button>
)rawhtml";
    html += R"rawhtml(                                            <button onClick={() => fetchRemoteFiles(remotePath)} class="p-1.5 rounded-lg border border-slate-700 hover:bg-slate-800 transition" title="Refresh">
                                                <RefreshIcon />
                                            </button>
                                        </div>
                                    </div>

                                    {/* Remote Path Navigation Bar */}
                                    <div class="mb-4">
                                        <input type="text" value={remoteInput} onChange={e => setRemoteInput(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') changeRemotePath(remoteInput); }} onBlur={() => changeRemotePath(remoteInput)} class="w-full glass-input rounded-lg py-1.5 px-3 text-sm font-mono text-slate-300 focus:ring-1 focus:ring-blue-500" />
                                    </div>

                                    {/* Remote Directory Table */}
                                    <div class="flex-1 overflow-auto rounded-lg border border-slate-800 bg-slate-950/40">
                                        <table class="w-full text-sm text-left text-slate-300">
                                            <thead class="sticky top-0 bg-slate-900 text-xs text-slate-400 uppercase tracking-wider border-b border-slate-800">
                                                <tr>
                                                    <th class="p-3 w-8"><input type="checkbox" checked={remoteSortedFiles.length > 0 && remoteSelected.size === remoteSortedFiles.length} onChange={e => {
                                                        if (e.target.checked) setRemoteSelected(new Set(remoteSortedFiles.map(f => f.name)));
                                                        else setRemoteSelected(new Set());
                                                    }} class="rounded bg-slate-900 border-slate-700 text-blue-500 focus:ring-blue-500"/></th>
                                                    <th class="p-3 cursor-pointer" onClick={() => setRemoteSort({ col: 'name', asc: !remoteSort.asc })}>Name</th>
                                                    <th class="p-3 cursor-pointer w-24" onClick={() => setRemoteSort({ col: 'size', asc: !remoteSort.asc })}>Size</th>
                                                    <th class="p-3 cursor-pointer w-40" onClick={() => setRemoteSort({ col: 'last_modified', asc: !remoteSort.asc })}>Modified</th>
                                                </tr>
                                            </thead>
                                            <tbody class="divide-y divide-slate-850">
                                                {remoteSortedFiles.map(file => (
                                                    <tr key={file.name} onDoubleClick={() => handleRemoteDoubleClick(file)} class={`hover:bg-slate-800/40 transition cursor-pointer select-none ${remoteSelected.has(file.name) ? 'bg-blue-500/10' : ''}`} onClick={() => toggleSelectRemote(file.name)}>
                                                        <td class="p-3" onClick={e => e.stopPropagation()}>
                                                            <input type="checkbox" checked={remoteSelected.has(file.name)} onChange={() => toggleSelectRemote(file.name)} class="rounded bg-slate-900 border-slate-700 text-blue-500 focus:ring-blue-500"/>
                                                        </td>
                                                        <td class="p-3 flex items-center font-medium max-w-[200px] truncate">
                                                            {file.is_dir ? <FolderIcon /> : <FileIcon />}
                                                            <span class="truncate">{file.name}</span>
                                                        </td>
                                                        <td class="p-3 text-slate-400 font-mono text-xs">{file.is_dir ? 'Folder' : formatBytes(file.size)}</td>
                                                        <td class="p-3 text-slate-400 text-xs">{formatDate(file.last_modified)}</td>
                                                    </tr>
                                                ))}
                                            </tbody>
                                        </table>
                                    </div>
                                    
                                    {/* Remote selection summary & action */}
                                    <div class="mt-3 flex items-center justify-between text-xs text-slate-400">
                                        <span>{remoteSelected.size} of {remoteFiles.length} items selected</span>
                                        <button onClick={() => startTransfer('download')} class="px-4 py-2 rounded-lg bg-emerald-600 hover:bg-emerald-500 text-white font-semibold flex items-center space-x-1 shadow-neon-green transition">
                                            <svg class="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M11 19l-7-7 7-7m8 14l-7-7 7-7"></path></svg>
                                            <span>Download to Local</span>
                                        </button>
                                    </div>
                                </React.Fragment>
                            ) : (
                                <div class="flex-1 flex flex-col items-center justify-center text-center p-8">
                                    <svg class="w-16 h-16 text-slate-600 mb-4" fill="none" stroke="currentColor" strokeWidth="1.5" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M5.636 18.364a9 9 0 010-12.728m12.728 0a9 9 0 010 12.728m-9.9-2.829a5 5 0 010-7.07m7.07 0a5 5 0 010 7.07M13 12a1 1 0 11-2 0 1 1 0 012 0z"></path></svg>
                                    <h3 class="text-lg font-semibold text-slate-300 mb-2">Server Connection Pending</h3>
                                    <p class="text-sm text-slate-500 max-w-sm mb-6">Connect to a remote NetCopy daemon to browse remote directories and synchronize files securely.</p>
                                    <button onClick={() => setShowConnectModal(true)} class="px-5 py-2.5 rounded-lg bg-blue-600 hover:bg-blue-500 text-white font-semibold shadow-neon-blue transition">Establish Connection</button>
                                </div>
                            )}
                        </div>

                    </div>
)rawhtml";
    html += R"rawhtml(                    {/* Console / Audit Log Drawer */}
                    <div class="glass-panel rounded-xl flex flex-col h-40 p-4">
                        <div class="text-xs font-bold text-slate-400 mb-2 tracking-widest uppercase flex items-center justify-between">
                            <span>System & Transfer Audit Logs</span>
                            <button onClick={() => setLogs([])} class="hover:text-slate-200 transition">Clear Console</button>
                        </div>
                        <div class="flex-1 overflow-y-auto font-mono text-[11px] text-slate-400 bg-slate-950/60 rounded-lg p-3 space-y-1">
                            {logs.map((log, idx) => (
                                <div key={idx} class={log.includes('[ERROR]') ? 'text-red-400' : log.includes('[WARNING]') ? 'text-amber-400' : 'text-slate-400'}>{log}</div>
                            ))}
                            <div ref={logEndRef} />
                        </div>
                    </div>

                    {/* Connection Modal */}
                    {showConnectModal && (
                        <div class="fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center p-4 z-50">
                            <div class="glass-panel w-full max-w-xl rounded-2xl p-6 shadow-2xl relative">
                                <button onClick={() => setShowConnectModal(false)} class="absolute top-4 right-4 text-slate-400 hover:text-slate-200 transition">
                                    <svg class="w-6 h-6" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12"></path></svg>
                                </button>
                                
                                <h3 class="text-xl font-bold text-slate-200 mb-4 flex items-center">
                                    <SettingsIcon />
                                    <span class="ml-2">Connect to NetCopy Remote Daemon</span>
                                </h3>

                                {/* Stored profile list */}
                                {profiles.length > 0 && (
                                    <div class="mb-4">
                                        <label class="block text-xs font-bold uppercase tracking-wider text-slate-400 mb-2">Stored Profiles</label>
                                        <div class="flex flex-wrap gap-2">
                                            {profiles.map(p => (
                                                <div key={p.name} class="inline-flex items-center bg-slate-800 rounded-lg border border-slate-700 transition text-xs text-slate-300 overflow-hidden">
                                                    <button type="button" onClick={() => applyProfile(p)} class="hover:bg-slate-700 py-1 px-2.5 border-r border-slate-700 transition font-medium">
                                                        {p.name}
                                                    </button>
                                                    <button type="button" onClick={() => deleteProfile(p.name)} class="hover:bg-red-900/40 text-slate-500 hover:text-red-400 py-1 px-2 transition font-bold" title="Delete Profile">
                                                        &times;
                                                    </button>
                                                </div>
                                            ))}
                                        </div>
                                    </div>
                                )}
)rawhtml";
    html += R"rawhtml(                                <form onSubmit={handleConnect} class="space-y-4">
                                    <div class="grid grid-cols-3 gap-3">
                                        <div class="col-span-2">
                                            <label class="block text-xs text-slate-400 mb-1">Server Address</label>
                                            <input type="text" placeholder="127.0.0.1" value={remoteHost} onChange={e => setRemoteHost(e.target.value)} required class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                        </div>
                                        <div>
                                            <label class="block text-xs text-slate-400 mb-1">Port</label>
                                            <input type="number" value={remotePort} onChange={e => setRemotePort(e.target.value)} required class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                        </div>
                                    </div>

                                    <div class="grid grid-cols-2 gap-3">
                                        <div>
                                            <label class="block text-xs text-slate-400 mb-1">Username (Empty for Anon)</label>
                                            <input type="text" placeholder="deploy_bot" value={remoteUsername} onChange={e => setRemoteUsername(e.target.value)} class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                        </div>
                                        <div>
                                            <label class="block text-xs text-slate-400 mb-1">Auth Method</label>
                                            <select value={remoteAuthMethod} onChange={e => setRemoteAuthMethod(e.target.value)} class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-300">
                                                <option value="none">Anonymous / PSK-only</option>
                                                <option value="password">Password Challenge</option>
                                                <option value="mlkem">Post-Quantum ML-KEM Key</option>
                                            </select>
                                        </div>
                                    </div>

                                    {remoteAuthMethod === 'password' && (
                                        <div>
                                            <label class="block text-xs text-slate-400 mb-1">Password</label>
                                            <input type="password" placeholder="••••••••" value={remotePassword} onChange={e => setRemotePassword(e.target.value)} required class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                        </div>
                                    )}

                                    {remoteAuthMethod === 'mlkem' && (
                                        <div class="grid grid-cols-2 gap-3">
                                            <div>
                                                <label class="block text-xs text-slate-400 mb-1">Private Key File Path (.pem)</label>
                                                <input type="text" placeholder="id_mlkem768.pem" value={remoteKeyFile} onChange={e => setRemoteKeyFile(e.target.value)} required class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                            </div>
                                            <div>
                                                <label class="block text-xs text-slate-400 mb-1">Private Key Passphrase</label>
                                                <input type="password" placeholder="Passphrase" value={remoteKeyPass} onChange={e => setRemoteKeyPass(e.target.value)} class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                            </div>
                                        </div>
                                    )}

                                    <div>
                                        <label class="block text-xs text-slate-400 mb-1">Initial Remote Path(s) (comma-separated list)</label>
                                        <input type="text" placeholder="D:\src, D:\Work\FILES" value={remoteAllowedPathsInput} onChange={e => setRemoteAllowedPathsInput(e.target.value)} class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                    </div>

                                    <hr class="border-slate-800" />

                                    <div class="grid grid-cols-3 gap-3">
                                        <div class="col-span-2">
                                            <label class="block text-xs text-slate-400 mb-1">Transport Encryption Secret Key (Hex)</label>
                                            <input type="text" value={remoteSecretKey} onChange={e => setRemoteSecretKey(e.target.value)} required class="w-full glass-input rounded-lg py-2 px-3 text-xs text-slate-200 font-mono truncate" />
                                        </div>
                                        <div>
                                            <label class="block text-xs text-slate-400 mb-1">Cipher Mode</label>
                                            <select value={remoteSecurityLevel} onChange={e => setRemoteSecurityLevel(e.target.value)} class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-300">
                                                <option value="high">ChaCha20-Poly1305</option>
                                                <option value="aes">AES-128-CTR (AES-NI)</option>
                                                <option value="AES-256-GCM">AES-256-GCM (GPU)</option>
                                                <option value="fast">XOR (Dev / Local)</option>
                                            </select>
                                        </div>
                                    </div>

                                    <div class="mt-6 flex justify-end space-x-3">
                                        <button type="button" onClick={saveProfile} class="px-4 py-2 rounded-lg border border-slate-700 hover:bg-slate-800 text-slate-300 text-sm font-semibold transition">Save Profile</button>
                                        <button type="button" onClick={() => setShowConnectModal(false)} class="px-4 py-2 rounded-lg border border-slate-700 hover:bg-slate-800 text-slate-300 text-sm font-semibold transition">Cancel</button>
                                        <button type="submit" class="px-5 py-2.5 rounded-lg bg-blue-600 hover:bg-blue-500 text-white text-sm font-semibold shadow-neon-blue transition">Establish Session</button>
                                    </div>
                                </form>
                            </div>
                        </div>
                    )}

                    {/* Draggable Full Cards */}
                    {transfers.filter(t => !t.minimized && !hiddenCards.has(t.id)).map((transfer, idx) => (
                        <TransferCard
                            key={transfer.id}
                            transfer={transfer}
                            index={idx}
                            cardPositions={cardPositions}
                            setCardPositions={setCardPositions}
                            hiddenCards={hiddenCards}
                            setHiddenCards={setHiddenCards}
                            onRemove={removeTransfer}
                            onShowServerStatus={showServerStatus}
                            onControlAll={controlAllFiles}
                            refreshExplorers={refreshExplorers}
                        />
                    ))}

                    {/* Minimized Badges Row */}
)rawhtml";
    html += R"rawhtml(                        {transfers.some(t => t.minimized && !hiddenCards.has(t.id)) && (
                            <div class="flex flex-wrap justify-end gap-2 max-w-sm pointer-events-auto bg-slate-900/80 backdrop-blur-md p-2 rounded-xl border border-slate-800 shadow-xl">
                                {transfers.filter(t => t.minimized && !hiddenCards.has(t.id)).map(transfer => {
                                    const percent = transfer.percent || 0;
                                    const isUpload = transfer.direction === 'upload';
                                    
                                    return (
                                        <div key={transfer.id} onClick={async () => {
                                            try {
                                                await fetch('/api/transfer/minimize', {
                                                    method: 'POST',
                                                    headers: { 'Content-Type': 'application/json' },
                                                    body: JSON.stringify({ id: transfer.id, minimized: false })
                                                });
                                            } catch (err) {
                                                console.error("Error restoring transfer:", err);
                                            }
                                        }} class={`flex items-center space-x-2 px-3 py-1.5 rounded-lg border text-xs font-semibold cursor-pointer transition select-none hover:bg-slate-800 ${isUpload ? 'border-blue-500/30 text-blue-400 bg-blue-500/5' : 'border-emerald-500/30 text-emerald-400 bg-emerald-500/5'}`} title="Click to restore">
                                            {transfer.active ? (
                                                <span class={`h-2 w-2 rounded-full animate-ping ${isUpload ? 'bg-blue-500' : 'bg-emerald-500'}`}></span>
                                            ) : (
                                                <span class={`h-2 w-2 rounded-full ${transfer.error ? 'bg-red-500' : 'bg-emerald-500'}`}></span>
                                            )}
                                            <span>
                                                {isUpload ? 'UP' : 'DL'} ({percent.toFixed(0)}%)
                                            </span>
                                            <svg class="w-3.5 h-3.5 text-slate-400" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                                                <path strokeLinecap="round" strokeLinejoin="round" d="M5 15l7-7 7 7" />
                                            </svg>
                                        </div>
                                    );
                                })}
                            </div>
                        )}

                        {/* History Session Drawer */}
                        {showHistoryDrawer && (
                            <div class="fixed inset-0 bg-black/60 backdrop-blur-sm flex justify-end z-50 pointer-events-auto">
                                <div class="absolute inset-0" onClick={() => setShowHistoryDrawer(false)}></div>
                                <div class="glass-panel w-full max-w-md h-full shadow-2xl relative p-6 flex flex-col bg-slate-950/95 border-l border-slate-800">
                                    <button onClick={() => setShowHistoryDrawer(false)} class="absolute top-4 right-4 text-slate-400 hover:text-slate-200 transition">
                                        <svg class="w-6 h-6" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12"></path></svg>
                                    </button>
                                    
                                    <h3 class="text-xl font-bold text-slate-200 mb-6 flex items-center border-b border-slate-800 pb-3">
                                        <svg class="w-6 h-6 text-indigo-400 mr-2" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
                                            <path strokeLinecap="round" strokeLinejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
                                        </svg>
                                        <span>Transfer Session History</span>
                                    </h3>

                                    <div class="flex-1 overflow-y-auto space-y-4 pr-1">
                                        {transfers.length === 0 ? (
                                            <div class="text-slate-500 text-center py-8 text-sm">
                                                No transfer sessions in this workspace.
                                            </div>
                                        ) : (
                                            transfers.map(t => {
                                                const isUpload = t.direction === 'upload';
                                                const percent = t.percent || 0;
                                                const isHidden = hiddenCards.has(t.id);
                                                
                                                return (
                                                    <div key={t.id} class="p-4 rounded-xl border border-slate-800 bg-slate-900/40 hover:bg-slate-900/60 transition flex flex-col space-y-3">
                                                        <div class="flex justify-between items-start">
                                                            <div class="flex flex-col">
                                                                <span class="text-xs font-mono text-slate-400">{t.start_time || 'Unknown start'}</span>
                                                                <span class="text-sm font-semibold text-slate-200 mt-1 capitalize">{t.direction}</span>
                                                            </div>
                                                            <span class={`text-[10px] font-bold uppercase px-2 py-0.5 rounded ${
                                                                t.active 
                                                                    ? 'bg-blue-500/20 text-blue-400 animate-pulse' 
                                                                    : (t.error ? 'bg-red-500/20 text-red-400' : 'bg-emerald-500/20 text-emerald-400')
                                                            }`}>
                                                                {t.active ? 'Active' : (t.error ? 'Stopped / Failed' : 'Completed')}
                                                            </span>
                                                        </div>
                                                        <div class="text-xs text-slate-400 space-y-1">
                                                            <div class="truncate"><span class="font-semibold text-slate-300">Src:</span> {t.source}</div>
                                                            <div class="truncate"><span class="font-semibold text-slate-300">Dst:</span> {t.destination}</div>
                                                            <div><span class="font-semibold text-slate-300">Progress:</span> {formatBytes(t.bytes_transferred)} / {formatBytes(t.total_bytes)} ({percent.toFixed(1)}%)</div>
                                                        </div>
                                                        <div class="flex justify-end items-center pt-1 border-t border-slate-800/40 mt-2">
                                                            {isHidden && (
                                                                <button onClick={() => {
                                                                    const updated = new Set(hiddenCards);
                                                                    updated.delete(t.id);
                                                                    setHiddenCards(updated);
                                                                }} class="px-2.5 py-1 bg-indigo-600 hover:bg-indigo-500 text-white text-[11px] font-semibold rounded transition">
                                                                    Restore Card
                                                                </button>
                                                            )}
                                                        </div>
                                                    </div>
                                                );
                                            })
                                        )}
                                    </div>
                                </div>
                            </div>
                        )}
)rawhtml";
    html += R"rawhtml(
                    {/* Server Session Info Modal */}
                    {showSessionModal && (
                        <div class="fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center p-4 z-50">
                            <div class="glass-panel w-full max-w-2xl rounded-2xl p-6 shadow-2xl relative flex flex-col max-h-[85vh] pointer-events-auto">
                                <button onClick={() => setShowSessionModal(false)} class="absolute top-4 right-4 text-slate-400 hover:text-slate-200 transition">
                                    <svg class="w-6 h-6" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12"></path></svg>
                                </button>
                                
                                <h3 class="text-xl font-bold text-slate-200 mb-4 flex items-center">
                                    <svg class="w-5 h-5 text-indigo-400 mr-2" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
                                        <path strokeLinecap="round" strokeLinejoin="round" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z"/>
                                    </svg>
                                    Remote Server Session Status
                                </h3>

                                {loadingSession && !sessionInfo && (
                                    <div class="flex-1 flex flex-col items-center justify-center py-12 space-y-3">
                                        <div class="animate-spin rounded-full h-8 w-8 border-b-2 border-indigo-500"></div>
                                        <p class="text-sm text-slate-450">Connecting to server and querying session metadata...</p>
                                    </div>
                                )}

                                {sessionError && (
                                    <div class="flex-1 flex flex-col items-center justify-center py-12 space-y-3 text-center">
                                        <svg class="w-12 h-12 text-red-500/80" fill="none" stroke="currentColor" strokeWidth="1.5" viewBox="0 0 24 24">
                                            <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z"/>
                                        </svg>
                                        <p class="text-sm text-red-400 font-semibold bg-red-950/20 p-4 rounded-lg border border-red-900/30 max-w-md">
                                            {sessionError}
                                        </p>
                                    </div>
                                )}

                                {sessionInfo && (
                                    <div class="flex-1 flex flex-col space-y-4 min-h-0">
                                        <div class="grid grid-cols-2 gap-4 bg-slate-900/60 p-4 rounded-xl border border-slate-800">
                                            <div>
                                                <span class="block text-[10px] uppercase font-bold text-slate-500 tracking-wider">Session ID</span>
                                                <span class="text-sm font-mono text-indigo-300 font-medium select-all">{sessionInfo.session_id}</span>
                                            </div>
                                            <div>
                                                <span class="block text-[10px] uppercase font-bold text-slate-500 tracking-wider">Status / State</span>
                                                <div class="flex items-center space-x-1.5 mt-0.5">
                                                    <span class={`h-2 w-2 rounded-full ${sessionInfo.active ? 'bg-emerald-500 animate-pulse shadow-neon-green' : 'bg-slate-500'}`}></span>
                                                    <span class="text-sm font-semibold text-slate-200">{sessionInfo.status_string}</span>
                                                </div>
                                            </div>
                                            <div class="col-span-2">
                                                <span class="block text-[10px] uppercase font-bold text-slate-500 tracking-wider">Server Stats</span>
                                                <span class="text-sm text-slate-350">{formatBytes(sessionInfo.bytes_transferred)} transferred of {formatBytes(sessionInfo.total_bytes)}</span>
                                            </div>
                                        </div>

                                        <div class="flex-1 flex flex-col min-h-0">
                                            <span class="block text-[10px] uppercase font-bold text-slate-500 tracking-wider mb-1">
                                                {sessionInfo.direction === 'upload' ? 'The following files had been uploaded to:' : 'The following files had been downloaded from:'}
                                            </span>
                                            <div class="text-xs font-mono text-indigo-300 font-semibold mb-3 select-all truncate bg-slate-950/60 p-2.5 rounded-lg border border-slate-850">
                                                {sessionInfo.destination}
                                            </div>

                                            <div class="flex-1 overflow-y-auto bg-slate-950/80 rounded-xl border border-slate-850 min-h-0">
                                                <table class="w-full text-left text-xs border-collapse">
                                                    <thead>
                                                        <tr class="bg-slate-900/60 text-slate-400 font-semibold border-b border-slate-800 uppercase tracking-wider text-[9px]">
                                                             <th class="p-2.5">File Name</th>
                                                             <th class="p-2.5 text-right">File Size</th>
                                                             <th class="p-2.5 text-right">Transferred</th>
                                                             <th class="p-2.5 text-right">Percentage</th>
                                                        </tr>
                                                    </thead>
                                                    <tbody class="divide-y divide-slate-900/40 text-slate-300">
                                                        {sessionInfo.files && sessionInfo.files.map((file, idx) => {
                                                            const percent = file.total_bytes > 0 ? (file.bytes_transferred / file.total_bytes) * 100 : 0;
                                                            const fileName = file.path.split(/[/\\]/).pop();
                                                            return (
                                                                <tr key={idx} class="hover:bg-slate-900/20 transition">
                                                                    <td class="p-2.5 font-mono truncate max-w-[220px]" title={file.path}>{fileName}</td>
                                                                    <td class="p-2.5 text-right font-mono">{formatBytes(file.total_bytes)}</td>
                                                                    <td class="p-2.5 text-right font-mono">{formatBytes(file.bytes_transferred)}</td>
                                                                    <td class="p-2.5 text-right font-mono font-semibold text-indigo-400">{percent.toFixed(1)}%</td>
                                                                </tr>
                                                            );
                                                        })}
                                                    </tbody>
                                                </table>
                                            </div>
                                        </div>
                                    </div>
                                )}
)rawhtml";
    html += R"rawhtml(                                
                                <div class="mt-4 flex justify-end">
                                    <button onClick={() => setShowSessionModal(false)} class="px-5 py-2 rounded-lg bg-slate-800 hover:bg-slate-755 text-slate-300 text-sm font-semibold border border-slate-700 transition">
                                        Close Details
                                    </button>
                                </div>
                            </div>
                        </div>
                    )}

                    {/* Create Folder Modal */}
                    {createFolderConfig.show && (
                        <div class="fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center p-4 z-50">
                            <div class="glass-panel w-full max-w-md rounded-2xl p-6 shadow-2xl relative pointer-events-auto">
                                <button onClick={() => setCreateFolderConfig(prev => ({ ...prev, show: false }))} class="absolute top-4 right-4 text-slate-400 hover:text-slate-200 transition">
                                    <svg class="w-6 h-6" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12"></path></svg>
                                </button>
                                
                                <h3 class="text-lg font-bold text-slate-200 mb-4 flex items-center">
                                    <svg class="w-5 h-5 text-amber-400 mr-2" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
                                        <path strokeLinecap="round" strokeLinejoin="round" d="M9 13h6m-3-3v6m-9 1V4a2 2 0 012-2h6l2 2h6a2 2 0 012 2v8a2 2 0 01-2 2H5a2 2 0 01-2-2z"/>
                                    </svg>
                                    Create New Folder ({createFolderConfig.type === 'local' ? 'Local' : 'Remote'})
                                </h3>

                                <div class="space-y-4">
                                    <div>
                                        <label class="block text-xs text-slate-400 mb-1">Parent Directory</label>
                                        <span class="block text-xs font-mono text-slate-300 bg-slate-950/60 p-2 rounded border border-slate-800/80 truncate select-all">{createFolderConfig.parentPath}</span>
                                    </div>

                                    <div>
                                        <label class="block text-xs text-slate-400 mb-1">Folder Name</label>
                                        <input type="text" placeholder="New Folder" value={createFolderConfig.folderName} onChange={e => setCreateFolderConfig(prev => ({ ...prev, folderName: e.target.value }))} onKeyDown={e => { if (e.key === 'Enter') { createNewFolder(createFolderConfig.type, createFolderConfig.parentPath, createFolderConfig.folderName); setCreateFolderConfig(prev => ({ ...prev, show: false })); } }} required class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                    </div>

                                    <div class="flex justify-end space-x-3 pt-2">
                                        <button onClick={() => setCreateFolderConfig(prev => ({ ...prev, show: false }))} class="px-4 py-2 rounded-lg border border-slate-700 hover:bg-slate-800 text-slate-350 text-sm font-semibold transition">Cancel</button>
                                        <button onClick={() => { createNewFolder(createFolderConfig.type, createFolderConfig.parentPath, createFolderConfig.folderName); setCreateFolderConfig(prev => ({ ...prev, show: false })); }} class="px-5 py-2.5 rounded-lg bg-blue-600 hover:bg-blue-500 text-white text-sm font-semibold shadow-neon-blue transition">Create Folder</button>
                                    </div>
                                </div>
                            </div>
                        </div>
                    )}

                    {/* Share Modal */}
                    {shareConfig.show && (
                        <div class="fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center p-4 z-50">
                            <div class="glass-panel w-full max-w-md rounded-2xl p-6 shadow-2xl relative pointer-events-auto">
                                <button onClick={() => setShareConfig(prev => ({ ...prev, show: false }))} class="absolute top-4 right-4 text-slate-400 hover:text-slate-200 transition">
                                    <svg class="w-6 h-6" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12"></path></svg>
                                </button>
                                
                                <h3 class="text-lg font-bold text-slate-200 mb-4 flex items-center">
                                    <svg class="w-5 h-5 text-blue-400 mr-2" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
                                        <path strokeLinecap="round" strokeLinejoin="round" d="M8.684 10.742a3 3 0 110-2.282m0 0A2.25 2.25 0 0110.5 6h4.5a2.25 2.25 0 012.25 2.25v1.5a2.25 2.25 0 01-2.25 2.25H11.5c-.868 0-1.637-.491-2.008-1.218z"/>
                                    </svg>
                                    Share File
                                </h3>

                                <div class="space-y-4">
                                    <div>
                                        <label class="block text-xs text-slate-400 mb-1">File Name</label>
                                        <span class="block text-xs font-mono text-slate-300 bg-slate-950/60 p-2 rounded border border-slate-800/80 truncate select-all">{shareConfig.fileName}</span>
                                    </div>

                                    {!shareConfig.shareUrl ? (
                                        <React.Fragment>
                                            <div>
                                                <label class="block text-xs text-slate-400 mb-1">Expiration</label>
                                                <select value={shareConfig.expirySeconds} onChange={e => setShareConfig(prev => ({ ...prev, expirySeconds: e.target.value }))} class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200">
                                                    <option value={3600}>1 Hour</option>
                                                    <option value={86400}>1 Day</option>
                                                    <option value={604800}>1 Week</option>
                                                    <option value={0}>Never Expire</option>
                                                </select>
                                            </div>

                                            <div>
                                                <label class="block text-xs text-slate-400 mb-1">Max Downloads (0 for unlimited)</label>
                                                <input type="number" min="0" value={shareConfig.maxDownloads} onChange={e => setShareConfig(prev => ({ ...prev, maxDownloads: e.target.value }))} class="w-full glass-input rounded-lg py-2 px-3 text-sm text-slate-200 font-mono" />
                                            </div>

                                            <div class="flex justify-end space-x-3 pt-2">
                                                <button onClick={() => setShareConfig(prev => ({ ...prev, show: false }))} class="px-4 py-2 rounded-lg border border-slate-700 hover:bg-slate-800 text-slate-350 text-sm font-semibold transition">Cancel</button>
                                                <button onClick={generateShareLink} class="px-5 py-2.5 rounded-lg bg-blue-600 hover:bg-blue-500 text-white text-sm font-semibold shadow-neon-blue transition">Generate Link</button>
                                            </div>
                                        </React.Fragment>
                                    ) : (
                                        <React.Fragment>
                                            <div>
                                                <label class="block text-xs text-slate-400 mb-1">Share URL</label>
                                                <div class="flex space-x-2">
                                                    <input type="text" readOnly value={shareConfig.shareUrl} class="flex-1 glass-input rounded-lg py-2 px-3 text-sm text-blue-400 font-mono" />
                                                    <button onClick={() => { navigator.clipboard.writeText(shareConfig.shareUrl); addLog("Copied link to clipboard!", "INFO"); }} class="px-3 py-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-200 text-sm font-semibold transition">Copy</button>
                                                </div>
                                            </div>
                                            
                                            <div class="flex justify-end pt-2">
                                                <button onClick={() => setShareConfig(prev => ({ ...prev, show: false }))} class="px-5 py-2 rounded-lg bg-blue-600 hover:bg-blue-500 text-white text-sm font-semibold shadow-neon-blue transition">Done</button>
                                            </div>
                                        </React.Fragment>
                                    )}
                                </div>
                            </div>
                        </div>
                    )}
                </React.Fragment>
            );
        }

        const root = ReactDOM.createRoot(document.getElementById('root'));
        root.render(<App />);
    </script>
</body>
</html>)rawhtml";
    return html;
}

inline const std::string GUI_HTML_CONTENT = build_gui_html();

} // namespace gui
} // namespace netcopy
