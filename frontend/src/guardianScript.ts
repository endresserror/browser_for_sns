import type { Platform } from './types'

export interface GuardianSettings {
  apiUrl: string
  enableAnalysis: boolean
  enablePatternDetection: boolean
  accessToken?: string
}

const guardianInjector = (settings: GuardianSettings) => {
  if (!settings.enableAnalysis && !settings.enablePatternDetection) return

  const PLATFORM_CONFIG: Record<Platform, { sendButtons: string[]; inputFields: string[]; originalPostSelectors: string[] }> = {
    x: {
      sendButtons: [
        'div[data-testid="tweetButtonInline"]',
        'div[data-testid="tweetButton"]',
        'button[data-testid="tweetButtonInline"]',
        'div[data-testid="replyButton"]',
      ],
      inputFields: [
        'div[data-testid="tweetTextarea_0"]',
        'div[data-testid="tweetTextarea_1"]',
        'div[contenteditable="true"][data-testid^="tweetTextarea"]',
        'div[role="textbox"][data-testid="tweetTextarea_0"]',
      ],
      originalPostSelectors: [
        'article[role="article"] div[data-testid="tweetText"]',
        'div[data-testid="conversation"] article div[data-testid="tweetText"]',
      ],
    },
    mastodon: {
      sendButtons: [
        'button[type="submit"][data-testid="compose-form-publish"]',
        'button[type="submit"][class*="compose-form__publish-button"]',
        'button[type="submit"][aria-label*="Toot"]',
      ],
      inputFields: [
        'textarea[name="text"]',
        'textarea[class*="compose-form__textarea"]',
        'div[role="textbox"][contenteditable="true"]',
      ],
      originalPostSelectors: ['.status__content', '.detailed-status__body'],
    },
    bluesky: {
      sendButtons: [
        'button[data-testid="composer-submit"]',
        'button[aria-label="Post"]',
        'button[role="button"][data-testid="postButton"]',
      ],
      inputFields: [
        'textarea[data-testid="composer-textarea"]',
        'textarea[aria-label*="What\'s up"]',
        'div[role="textbox"][contenteditable="true"]',
      ],
      originalPostSelectors: ['div[data-testid="postThread"] article', 'article'],
    },
  }

  const detectPlatform = (): Platform | null => {
    const host = window.location.hostname
    if (host.includes('twitter.com') || host.includes('x.com')) return 'x'
    if (host.includes('mastodon')) return 'mastodon'
    if (host.includes('bsky.app')) return 'bluesky'
    return null
  }

  const textFromElement = (el: Element | null): string => {
    if (!el) return ''
    const anyEl = el as HTMLTextAreaElement
    if (typeof anyEl.value === 'string') return anyEl.value.trim()
    return (el.textContent || '').trim()
  }

  const getComposeText = (config: (typeof PLATFORM_CONFIG)[Platform]) => {
    for (const selector of config.inputFields) {
      const el = document.querySelector(selector)
      const text = textFromElement(el)
      if (text) return text
    }
    return ''
  }

  const extractOriginalPost = (config: (typeof PLATFORM_CONFIG)[Platform]) => {
    for (const selector of config.originalPostSelectors) {
      const el = document.querySelector(selector)
      const text = textFromElement(el)
      if (text) return text
    }
    return ''
  }

  const injectStyles = () => {
    if (document.getElementById('sns-guardian-style')) return
    const style = document.createElement('style')
    style.id = 'sns-guardian-style'
    style.textContent = `
      .sns-guardian-toast {
        position: fixed;
        top: 12px;
        right: 12px;
        background: #ffffff;
        color: #0f172a;
        border: 1px solid #e5e7eb;
        border-radius: 12px;
        padding: 10px 14px;
        box-shadow: 0 10px 30px rgba(15, 23, 42, 0.14);
        font-family: 'Inter', 'Noto Sans JP', system-ui, sans-serif;
        font-size: 14px;
        z-index: 2147483645;
      }
      .sns-guardian-overlay {
        position: fixed;
        inset: 0;
        background: rgba(15, 23, 42, 0.28);
        display: flex;
        align-items: center;
        justify-content: center;
        z-index: 2147483646;
      }
      .sns-guardian-modal {
        width: min(520px, calc(100% - 24px));
        background: #ffffff;
        border: 1px solid #e5e7eb;
        border-radius: 14px;
        padding: 18px 18px 14px;
        box-shadow: 0 16px 44px rgba(15, 23, 42, 0.24);
        font-family: 'Inter', 'Noto Sans JP', system-ui, sans-serif;
        color: #0f172a;
      }
      .sns-guardian-header { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 12px; }
      .sns-guardian-title { font-size: 16px; font-weight: 700; color: #0f172a; margin: 0; }
      .sns-guardian-chip { font-size: 12px; padding: 4px 10px; border-radius: 999px; background: #eff6ff; color: #1d4ed8; border: 1px solid #bfdbfe; font-weight: 600; }
      .sns-guardian-risk { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 10px; }
      .sns-guardian-tile { background: #f7f8fb; border: 1px solid #e5e7eb; border-radius: 10px; padding: 10px 12px; min-height: 60px; }
      .sns-guardian-label { font-size: 12px; color: #6b7280; margin-bottom: 4px; }
      .sns-guardian-score { font-size: 18px; font-weight: 700; margin: 0; }
      .sns-guardian-list { list-style: disc; padding-left: 18px; margin: 6px 0 0; color: #111827; font-size: 13px; display: grid; gap: 4px; }
      .sns-guardian-section-title { font-weight: 700; font-size: 13px; margin: 12px 0 6px; color: #0f172a; }
      .sns-guardian-actions { display: flex; justify-content: flex-end; gap: 8px; margin-top: 14px; }
      .sns-guardian-button { border-radius: 12px; border: 1px solid #e5e7eb; padding: 10px 14px; font-weight: 700; cursor: pointer; font-size: 14px; background: #ffffff; color: #0f172a; transition: all 0.15s ease; }
      .sns-guardian-button:hover { border-color: #cbd5e1; }
      .sns-guardian-button.primary { background: #2563eb; color: #ffffff; border-color: #2563eb; box-shadow: 0 10px 30px rgba(37, 99, 235, 0.18); }
      .sns-guardian-button.danger { background: #ffffff; border-color: #fcd34d; color: #b45309; }
      .sns-guardian-pattern { background: #fff7ed; border: 1px solid #fed7aa; border-radius: 10px; padding: 10px; margin-top: 10px; }
    `
    document.head.appendChild(style)
  }

  const showToast = (message: string) => {
    injectStyles()
    const toast = document.createElement('div')
    toast.className = 'sns-guardian-toast'
    toast.textContent = message
    document.body.appendChild(toast)
    return () => toast.remove()
  }

  const riskColor = (level: string) => {
    if (level === 'high') return '#ef4444'
    if (level === 'medium') return '#f59e0b'
    return '#16a34a'
  }

  const showModal = (analysis: any, pattern?: any): Promise<boolean> => {
    injectStyles()
    const riskPercent = Math.round((analysis?.risk_score || 0) * 100)
    const factors = analysis?.risk_factors?.length ? analysis.risk_factors : ['要因情報なし']
    const suggestions = analysis?.suggestions?.length ? analysis.suggestions : ['特になし']

    return new Promise((resolve) => {
      const overlay = document.createElement('div')
      overlay.className = 'sns-guardian-overlay'

      const modal = document.createElement('div')
      modal.className = 'sns-guardian-modal'

      const patternBlock = pattern
        ? `<div class="sns-guardian-pattern">
            <div class="sns-guardian-label">議論パターン検知</div>
            <div class="sns-guardian-score" style="color:${pattern.has_pattern ? '#ea580c' : '#16a34a'}">
              ${pattern.has_pattern ? pattern.pattern_type || '注意' : 'パターンなし'}
              ${pattern.confidence ? `(${Math.round(pattern.confidence * 100)}%)` : ''}
            </div>
            <div style="font-size:12px; color:#0f172a; margin-top:4px;">${pattern.explanation || ''}</div>
         </div>`
        : ''

      modal.innerHTML = `
        <div class="sns-guardian-header">
          <p class="sns-guardian-title">送信前チェック</p>
          <span class="sns-guardian-chip">SNS Guardian</span>
        </div>
        <div class="sns-guardian-risk">
          <div class="sns-guardian-tile">
            <div class="sns-guardian-label">リスクスコア</div>
            <p class="sns-guardian-score" style="color:${riskColor(analysis?.risk_level || 'low')}">${riskPercent}% (${analysis?.risk_level || 'low'})</p>
          </div>
          <div class="sns-guardian-tile">
            <div class="sns-guardian-label">主要要因</div>
            <ul class="sns-guardian-list">${factors.map((factor: string) => `<li>${factor}</li>`).join('')}</ul>
          </div>
        </div>
        <div>
          <div class="sns-guardian-section-title">改善のヒント</div>
          <ul class="sns-guardian-list">${suggestions.map((s: string) => `<li>${s}</li>`).join('')}</ul>
        </div>
        ${patternBlock}
        <div class="sns-guardian-actions">
          <button class="sns-guardian-button danger" data-action="cancel">投稿を中止</button>
          <button class="sns-guardian-button primary" data-action="continue">それでも投稿</button>
        </div>
      `

      overlay.appendChild(modal)
      document.body.appendChild(overlay)

      const cleanup = () => overlay.remove()

      modal.querySelector('[data-action="cancel"]')?.addEventListener('click', () => {
        cleanup()
        resolve(false)
      })

      modal.querySelector('[data-action="continue"]')?.addEventListener('click', () => {
        cleanup()
        resolve(true)
      })
    })
  }

  const analyze = async (payload: { text: string; platform: Platform; replying_to?: string }) => {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' }
    if (settings.accessToken) headers.Authorization = `Bearer ${settings.accessToken}`
    try {
      const res = await fetch(`${settings.apiUrl.replace(/\/$/, '')}/analysis/tweet`, {
        method: 'POST',
        headers,
        body: JSON.stringify(payload),
      })
      if (!res.ok) throw new Error('analysis failed')
      return res.json()
    } catch (error) {
      console.debug('analysis fallback', error)
      return {
        risk_level: 'low',
        risk_score: 0.1,
        risk_factors: ['分析サービスに接続できませんでした'],
        suggestions: ['時間を置いて再度お試しください', '投稿内容をもう一度読み返しましょう'],
      }
    }
  }

  const checkPattern = async (payload: { text: string; platform: Platform; context?: string }) => {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' }
    if (settings.accessToken) headers.Authorization = `Bearer ${settings.accessToken}`
    try {
      const res = await fetch(`${settings.apiUrl.replace(/\/$/, '')}/analysis/discussion-pattern`, {
        method: 'POST',
        headers,
        body: JSON.stringify(payload),
      })
      if (!res.ok) throw new Error('pattern failed')
      return res.json()
    } catch (error) {
      console.debug('pattern fallback', error)
      return { has_pattern: false, explanation: '検知サービスに接続できませんでした', confidence: 0 }
    }
  }

  const attachInterceptor = (platform: Platform, config: (typeof PLATFORM_CONFIG)[Platform]) => {
    const attach = () => {
      const selectors = config.sendButtons.join(',')
      document.querySelectorAll<HTMLElement>(selectors).forEach((button) => {
        if ((button as any).dataset.sgGuardianBound === 'true') return
        ;(button as any).dataset.sgGuardianBound = 'true'

        const handler = async (event: Event) => {
          if (!settings.enableAnalysis && !settings.enablePatternDetection) return
          if ((button as any).dataset.sgGuardianBypass === 'true') return
          event.preventDefault()
          event.stopPropagation()

          const dismiss = showToast('分析中...')
          try {
            const text = getComposeText(config)
            const replyingTo = extractOriginalPost(config)
            const analysis = settings.enableAnalysis
              ? await analyze({ text, platform, replying_to: replyingTo || undefined })
              : {
                  risk_level: 'low',
                  risk_score: 0,
                  risk_factors: ['分析はオフです'],
                  suggestions: [],
                }

            const pattern = settings.enablePatternDetection && replyingTo
              ? await checkPattern({ text, context: replyingTo, platform })
              : undefined

            dismiss()
            const allow = await showModal(analysis, pattern)
            if (allow) {
              ;(button as any).dataset.sgGuardianBypass = 'true'
              button.removeEventListener('click', handler, true)
              ;(button as HTMLButtonElement).click()
              setTimeout(() => {
                delete (button as any).dataset.sgGuardianBypass
                ;(button as any).dataset.sgGuardianBound = 'true'
                button.addEventListener('click', handler, true)
              }, 200)
            }
          } catch (error) {
            console.error('analysis error', error)
            dismiss()
            const allow = await showModal(
              {
                risk_level: 'low',
                risk_score: 0.08,
                risk_factors: ['分析中に問題が発生しました'],
                suggestions: ['ネットワークを確認してください', '不安な場合は投稿を控えましょう'],
              },
              undefined,
            )
            if (allow) {
              ;(button as any).dataset.sgGuardianBypass = 'true'
              button.removeEventListener('click', handler, true)
              ;(button as HTMLButtonElement).click()
              setTimeout(() => {
                delete (button as any).dataset.sgGuardianBypass
                ;(button as any).dataset.sgGuardianBound = 'true'
                button.addEventListener('click', handler, true)
              }, 200)
            }
          }
        }

        button.addEventListener('click', handler, true)
      })
    }

    attach()
    const observer = new MutationObserver(attach)
    observer.observe(document.body, { childList: true, subtree: true })
  }

  const platform = detectPlatform()
  if (!platform) return
  attachInterceptor(platform, PLATFORM_CONFIG[platform])
}

export const buildGuardianScript = (settings: GuardianSettings) => `(${guardianInjector.toString()})(${JSON.stringify(settings)});`
