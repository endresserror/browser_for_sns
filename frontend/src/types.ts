export type Platform = 'x' | 'mastodon' | 'bluesky'

export interface AnalysisResult {
  risk_level: 'low' | 'medium' | 'high' | string
  risk_score: number
  risk_factors: string[]
  suggestions: string[]
}

export interface PatternResult {
  has_pattern: boolean
  pattern_type?: string
  confidence?: number
  explanation?: string
}
