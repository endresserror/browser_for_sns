import type { WebviewTag } from 'electron'
import type React from 'react'

declare global {
  namespace JSX {
    interface IntrinsicElements {
      webview: React.DetailedHTMLProps<React.HTMLAttributes<WebviewTag>, WebviewTag> & {
        src?: string
        allowpopups?: boolean
      }
    }
  }
}

export {}
