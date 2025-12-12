import { useEffect, useRef, useState } from 'react'
import type { KeyboardEvent } from 'react'
import type { WebviewTag } from 'electron'
import { buildGuardianScript, type GuardianSettings } from './guardianScript'

const DEFAULT_SETTINGS: GuardianSettings = {
  apiUrl: 'http://localhost:8000/api/v1',
  enableAnalysis: true,
  enablePatternDetection: true,
  accessToken: '',
}

const QUICK_LINKS = [
  { label: 'X / Twitter', url: 'https://x.com' },
  { label: 'Mastodon', url: 'https://mastodon.social' },
  { label: 'Bluesky', url: 'https://bsky.app' },
]

function normalizeUrl(input: string): string {
  const trimmed = input.trim()
  if (!trimmed) return 'https://x.com'
  if (!/^https?:\/\//i.test(trimmed)) return `https://${trimmed}`
  return trimmed
}

function loadSettings(): GuardianSettings {
  try {
    const saved = localStorage.getItem('guardian-settings')
    if (saved) {
      const parsed = JSON.parse(saved) as Partial<GuardianSettings>
      return { ...DEFAULT_SETTINGS, ...parsed }
    }
  } catch (error) {
    console.warn('設定の読み込みに失敗しました', error)
  }
  return DEFAULT_SETTINGS
}

const App = () => {
  const [urlInput, setUrlInput] = useState('https://x.com')
  const [currentUrl, setCurrentUrl] = useState('https://x.com')
  const [status, setStatus] = useState('準備完了')
  const [settings, setSettings] = useState<GuardianSettings>(() => loadSettings())
  const webviewRef = useRef<WebviewTag | null>(null)

  useEffect(() => {
    localStorage.setItem('guardian-settings', JSON.stringify(settings))
    if (webviewRef.current) {
      injectGuardian()
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [settings.enableAnalysis, settings.enablePatternDetection, settings.apiUrl, settings.accessToken])

  useEffect(() => {
    const view = webviewRef.current
    if (!view) return

    const handleDomReady = () => {
      setStatus('読み込み完了')
      injectGuardian()
    }

    const handleFail = () => setStatus('読み込みに失敗しました')

    const handleNavigate = (event: any) => {
      if (event.url) {
        setCurrentUrl(event.url)
        setUrlInput(event.url)
      }
    }

    view.addEventListener('dom-ready', handleDomReady)
    view.addEventListener('did-fail-load', handleFail)
    view.addEventListener('did-navigate', handleNavigate)
    view.addEventListener('did-navigate-in-page', handleNavigate)

    return () => {
      view.removeEventListener('dom-ready', handleDomReady)
      view.removeEventListener('did-fail-load', handleFail)
      view.removeEventListener('did-navigate', handleNavigate)
      view.removeEventListener('did-navigate-in-page', handleNavigate)
    }
  }, [])

  const injectGuardian = () => {
    const view = webviewRef.current
    if (!view) return
    const script = buildGuardianScript(settings)
    view
      .executeJavaScript(script)
      .then(() => setStatus('ガーディアン適用済み'))
      .catch((error) => {
        console.error('ガーディアン注入に失敗', error)
        setStatus('注入に失敗しました')
      })
  }

  const navigate = (target?: string) => {
    const next = normalizeUrl(target || urlInput)
    setCurrentUrl(next)
    setUrlInput(next)
    setStatus('読み込み中')
    webviewRef.current?.loadURL(next)
  }

  const onEnter = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'Enter') {
      navigate()
    }
  }

  return (
    <div className="app">
      <header className="topbar">
        <div>
          <p className="eyebrow">SNS Guardian Browser</p>
          <h1 className="title">シンプルモデレーションブラウザ</h1>
        </div>
        <span className="status">{status}</span>
      </header>

      <div className="address-bar">
        <div className="controls">
          <button onClick={() => webviewRef.current?.goBack()} aria-label="戻る">
            ←
          </button>
          <button onClick={() => webviewRef.current?.goForward()} aria-label="進む">
            →
          </button>
          <button onClick={() => webviewRef.current?.reload()} aria-label="再読み込み">
            ↻
          </button>
        </div>
        <input
          value={urlInput}
          onChange={(e) => setUrlInput(e.target.value)}
          onKeyDown={onEnter}
          className="url-input"
          spellCheck={false}
        />
        <button className="primary" onClick={() => navigate()}>
          開く
        </button>
        <div className="quick-links">
          {QUICK_LINKS.map((link) => (
            <button key={link.url} onClick={() => navigate(link.url)} className="ghost">
              {link.label}
            </button>
          ))}
        </div>
      </div>

      <div className="layout">
        <aside className="panel">
          <div className="panel-header">
            <h3>ガーディアン設定</h3>
            <span className="pill">簡素UI</span>
          </div>
          <label className="field">
            <span>API ベースURL</span>
            <input
              type="url"
              value={settings.apiUrl}
              onChange={(e) => setSettings({ ...settings, apiUrl: e.target.value })}
              placeholder="http://localhost:8000/api/v1"
            />
          </label>
          <label className="field inline">
            <input
              type="checkbox"
              checked={settings.enableAnalysis}
              onChange={(e) => setSettings({ ...settings, enableAnalysis: e.target.checked })}
            />
            <div>
              <span>投稿前リスク分析</span>
              <p className="muted">送信前にAI分析を実行します。</p>
            </div>
          </label>
          <label className="field inline">
            <input
              type="checkbox"
              checked={settings.enablePatternDetection}
              onChange={(e) => setSettings({ ...settings, enablePatternDetection: e.target.checked })}
            />
            <div>
              <span>議論パターン検知</span>
              <p className="muted">返信時に炎上しやすいパターンを検知します。</p>
            </div>
          </label>
          <label className="field">
            <span>アクセストークン (任意)</span>
            <input
              type="text"
              value={settings.accessToken}
              onChange={(e) => setSettings({ ...settings, accessToken: e.target.value })}
              placeholder="Bearer トークン"
            />
          </label>
          <div className="hint">
            このブラウザはシンプルなデザインで、絵文字は使用していません。
          </div>
          <button className="secondary" onClick={injectGuardian}>
            現在のページに再適用
          </button>
        </aside>

        <section className="browser-area">
          <webview
            ref={webviewRef as any}
            src={currentUrl}
            allowpopups
            style={{ width: '100%', height: '100%', border: '1px solid #e5e7eb', borderRadius: 12 }}
          />
        </section>
      </div>
    </div>
  )
}

export default App
