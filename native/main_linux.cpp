#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <algorithm>

// シンプルな Linux 向け SNS ブラウザ (GTK + WebKit2GTK)。
// 送信前にガーディアンスクリプトを挿入して炎上リスクを表示します。

namespace {

enum class AnalysisProvider {
    Api,
    Gemini,
    LocalHeuristic
};

struct GuardianSettings {
    std::string api_url = "http://localhost:8000/api/v1";
    std::string gemini_api_key{};
    std::string gemini_model = "gemini-1.5-flash-latest";
    AnalysisProvider provider = AnalysisProvider::Local; // デフォルトはローカル簡易検知
    bool enable_analysis = true;            // 高度分析は初期オン（ローカルのみでも動作）
    bool enable_pattern = true;             // パターン検知は初期オン（ローカルのみでも動作）
};

struct AppState {
    GtkWidget* window = nullptr;
    GtkWidget* web_view = nullptr;
    GtkWidget* api_entry = nullptr;
    GtkWidget* provider_combo = nullptr;
    GtkWidget* gemini_key_entry = nullptr;
    GtkWidget* gemini_model_entry = nullptr;
    GtkWidget* toggle_analysis = nullptr;
    GtkWidget* toggle_pattern = nullptr;
    GuardianSettings settings{};
};

std::string js_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\'': out += "\\\'"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string provider_to_string(AnalysisProvider provider) {
    switch (provider) {
    case AnalysisProvider::Gemini: return "gemini";
    case AnalysisProvider::LocalHeuristic: return "local";
    default: return "api";
    }
}

AnalysisProvider string_to_provider(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "gemini") return AnalysisProvider::Gemini;
    if (lower == "local" || lower == "heuristic") return AnalysisProvider::LocalHeuristic;
    return AnalysisProvider::Api;
}

bool parse_bool_env(const char* value, bool fallback) {
    if (!value) return fallback;
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

GuardianSettings load_settings_from_env() {
    GuardianSettings settings{};
    if (const char* api = std::getenv("SNS_GUARDIAN_API_URL")) {
        settings.api_url = api;
    }
    if (const char* provider = std::getenv("SNS_GUARDIAN_PROVIDER")) {
        settings.provider = string_to_provider(provider);
    }
    settings.enable_analysis = parse_bool_env(std::getenv("SNS_GUARDIAN_ENABLE_ANALYSIS"), settings.enable_analysis);
    settings.enable_pattern = parse_bool_env(std::getenv("SNS_GUARDIAN_ENABLE_PATTERN"), settings.enable_pattern);
    if (const char* key = std::getenv("SNS_GUARDIAN_GEMINI_API_KEY")) {
        settings.gemini_api_key = key;
    }
    if (const char* model = std::getenv("SNS_GUARDIAN_GEMINI_MODEL")) {
        settings.gemini_model = model;
    }
    return settings;
}

std::string normalize_url(const std::string& input) {
    std::string trimmed = input;
    while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    if (trimmed.empty()) return "https://x.com";
    if (trimmed.rfind("http://", 0) == 0 || trimmed.rfind("https://", 0) == 0) return trimmed;
    return "https://" + trimmed;
}

std::string build_guardian_script(const GuardianSettings& settings) {
    std::ostringstream ss;
    ss << R"JS((function(){
      const settings={
        apiUrl:')" << js_escape(settings.api_url) << R"JS(',
        analysisProvider:')" << provider_to_string(settings.provider) << R"JS(',
        geminiApiKey:')" << js_escape(settings.gemini_api_key) << R"JS(',
        geminiModel:')" << js_escape(settings.gemini_model) << R"JS(',
        enableAnalysis:)" << (settings.enable_analysis ? "true" : "false") << R"JS(,
        enablePatternDetection:)" << (settings.enable_pattern ? "true" : "false") << R"JS(
      };

      const WORKFLOW_TEMPLATE=()=>[
        {id:'capture',label:'入力テキストと返信先の取得',result:'pending'},
        {id:'analysis',label:`リスク分析 (ローカル簡易${settings.enableAnalysis?'+AI/API':''})`,result:'pending'},
        {id:'pattern',label:'議論パターン検知',result:'pending'},
        {id:'review',label:'結果レビューと送信判断',result:'pending'}
      ];
      const workflowLog=[];
      const logStep=(id,label,result)=>{workflowLog.push({id,label,result});};
      const unique=(arr)=>Array.from(new Set((arr||[]).filter(Boolean)));
      const combineAnalysis=(baseline,advanced)=>{
        if(!advanced){
          return {...baseline,risk_factors:unique([...baseline.risk_factors,'API未使用:ローカル簡易チェック'])};
        }
        const score=Math.max(baseline.risk_score||0,advanced.risk_score||0);
        const level=score>0.45?'high':score>0.25?'medium':'low';
        return{
          risk_level:level,
          risk_score:score,
          risk_factors:unique([...baseline.risk_factors,...(advanced.risk_factors||[])]),
          suggestions:unique([...(advanced.suggestions||[]),...(baseline.suggestions||[])])
        };
      };

      const PLATFORM_CONFIG={
        x:{sendButtons:['div[data-testid="tweetButtonInline"]','div[data-testid="tweetButton"]','button[data-testid="tweetButtonInline"]','div[data-testid="replyButton"]'],inputFields:['div[data-testid="tweetTextarea_0"]','div[data-testid="tweetTextarea_1"]','div[contenteditable="true"][data-testid^="tweetTextarea"]','div[role="textbox"][data-testid="tweetTextarea_0"]'],originalPostSelectors:['article[role="article"] div[data-testid="tweetText"]','div[data-testid="conversation"] article div[data-testid="tweetText"]']},
        mastodon:{sendButtons:['button[type="submit"][data-testid="compose-form-publish"]','button[type="submit"][class*="compose-form__publish-button"]','button[type="submit"][aria-label*="Toot"]'],inputFields:['textarea[name="text"]','textarea[class*="compose-form__textarea"]','div[role="textbox"][contenteditable="true"]'],originalPostSelectors:['.status__content','.detailed-status__body']},
        bluesky:{sendButtons:['button[data-testid="composer-submit"]','button[aria-label="Post"]','button[role="button"][data-testid="postButton"]'],inputFields:['textarea[data-testid="composer-textarea"]','textarea[aria-label*="What\\'s up"]','div[role="textbox"][contenteditable="true"]'],originalPostSelectors:['div[data-testid="postThread"] article','article']}
      };
      const detectPlatform=()=>{const h=location.hostname;if(h.includes('twitter.com')||h.includes('x.com'))return'x';if(h.includes('mastodon'))return'mastodon';if(h.includes('bsky.app'))return'bluesky';return null;};
      const textFromElement=(el)=>{if(!el)return'';if(typeof el.value==='string')return el.value.trim();return(el.textContent||'').trim();};
      const getComposeText=(cfg)=>{for(const s of cfg.inputFields){const el=document.querySelector(s);const t=textFromElement(el);if(t)return t;}return'';};
      const extractOriginalPost=(cfg)=>{for(const s of cfg.originalPostSelectors){const el=document.querySelector(s);const t=textFromElement(el);if(t)return t;}return'';};

      const injectStyles=()=>{if(document.getElementById('sg-style'))return;const style=document.createElement('style');style.id='sg-style';style.textContent=`
        .sg-toast{position:fixed;top:12px;right:12px;background:#fff;color:#0f172a;border:1px solid #e5e7eb;border-radius:12px;padding:10px 14px;box-shadow:0 10px 30px rgba(15,23,42,0.14);font-family:Inter,'Noto Sans JP',sans-serif;font-size:14px;z-index:2147483645;}
        .sg-overlay{position:fixed;inset:0;background:rgba(15,23,42,0.28);display:flex;align-items:center;justify-content:center;z-index:2147483646;}
        .sg-modal{width:min(560px,calc(100%-24px));background:#fff;border:1px solid #e5e7eb;border-radius:14px;padding:18px 18px 14px;box-shadow:0 16px 44px rgba(15,23,42,0.24);font-family:Inter,'Noto Sans JP',sans-serif;color:#0f172a;}
        .sg-header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px;}
        .sg-title{font-size:16px;font-weight:700;margin:0;color:#0f172a;}
        .sg-chip{font-size:12px;padding:4px 10px;border-radius:999px;background:#eff6ff;color:#1d4ed8;border:1px solid #bfdbfe;font-weight:600;}
        .sg-risk{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px;}
        .sg-tile{background:#f7f8fb;border:1px solid #e5e7eb;border-radius:10px;padding:10px 12px;min-height:60px;}
        .sg-label{font-size:12px;color:#6b7280;margin-bottom:4px;}
        .sg-score{font-size:18px;font-weight:700;margin:0;}
        .sg-list{list-style:disc;padding-left:18px;margin:6px 0 0;color:#111827;font-size:13px;display:grid;gap:4px;}
        .sg-section{font-weight:700;font-size:13px;margin:12px 0 6px;color:#0f172a;}
        .sg-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px;}
        .sg-btn{border-radius:12px;border:1px solid #e5e7eb;padding:10px 14px;font-weight:700;cursor:pointer;font-size:14px;background:#fff;color:#0f172a;transition:all .15s ease;}
        .sg-btn:hover{border-color:#cbd5e1;}
        .sg-btn.primary{background:#2563eb;color:#fff;border-color:#2563eb;box-shadow:0 10px 30px rgba(37,99,235,0.18);}
        .sg-btn.danger{background:#fff;border-color:#fcd34d;color:#b45309;}
        .sg-pattern{background:#fff7ed;border:1px solid #fed7aa;border-radius:10px;padding:10px;margin-top:10px;}
        .sg-steps{list-style:decimal;padding-left:18px;margin:6px 0 0;color:#0f172a;font-size:13px;display:grid;gap:4px;}
        .sg-step-row{display:flex;justify-content:space-between;gap:8px;}
        .sg-step-result{color:#334155;font-weight:600;}
      `;document.head.appendChild(style);};
      const showToast=(msg)=>{injectStyles();const t=document.createElement('div');t.className='sg-toast';t.textContent=msg;document.body.appendChild(t);return()=>t.remove();};
      const riskColor=(lvl)=>lvl==='high'?'#ef4444':lvl==='medium'?'#f59e0b':'#16a34a';
      const showModal=(analysis,pattern,steps)=>{injectStyles();return new Promise((resolve)=>{const overlay=document.createElement('div');overlay.className='sg-overlay';const modal=document.createElement('div');modal.className='sg-modal';const riskPercent=Math.round((analysis?.risk_score||0)*100);const factors=analysis?.risk_factors?.length?analysis.risk_factors:['要因情報なし'];const suggestions=analysis?.suggestions?.length?analysis.suggestions:['特になし'];const patternBlock=pattern?`<div class="sg-pattern">\n<div class="sg-label">議論パターン検知</div>\n<div class="sg-score" style="color:${pattern.has_pattern?'#ea580c':'#16a34a'}">${pattern.has_pattern?pattern.pattern_type||'注意':'パターンなし'} ${pattern.confidence?`(${Math.round(pattern.confidence*100)}%)`:''}</div>\n<div style=\"font-size:12px;color:#0f172a;margin-top:4px;\">${pattern.explanation||''}</div>\n</div>`:'';const workflowHtml=steps.map(s=>`<li class="sg-step-row"><span>${s.label}</span><span class="sg-step-result">${s.result}</span></li>`).join('');modal.innerHTML=`<div class=\"sg-header\"><p class=\"sg-title\">送信前チェック</p><span class=\"sg-chip\">SNS Guardian</span></div><div class=\"sg-risk\"><div class=\"sg-tile\"><div class=\"sg-label\">リスクスコア</div><p class=\"sg-score\" style=\"color:${riskColor(analysis?.risk_level||'low')}\">${riskPercent}% (${analysis?.risk_level||'low'})</p></div><div class=\"sg-tile\"><div class=\"sg-label\">主要要因</div><ul class=\"sg-list\">${factors.map(f=>`<li>${f}</li>`).join('')}</ul></div></div><div><div class=\"sg-section\">改善のヒント</div><ul class=\"sg-list\">${suggestions.map(s=>`<li>${s}</li>`).join('')}</ul></div><div><div class=\"sg-section\">ワークフロー</div><ol class=\"sg-steps\">${workflowHtml}</ol></div>${patternBlock}<div class=\"sg-actions\"><button class=\"sg-btn danger\" data-action=\"cancel\">投稿を中止</button><button class=\"sg-btn primary\" data-action=\"continue\">それでも投稿</button></div>`;overlay.appendChild(modal);document.body.appendChild(overlay);const cleanup=()=>overlay.remove();modal.querySelector('[data-action="cancel"]').addEventListener('click',()=>{cleanup();resolve(false);});modal.querySelector('[data-action="continue"]').addEventListener('click',()=>{cleanup();resolve(true);});});};

      const runApiAnalysis=async(payload)=>{const headers={'Content-Type':'application/json'};const url=`${settings.apiUrl.replace(/\\/$/,'')}/analysis/tweet`;const res=await fetch(url,{method:'POST',headers,body:JSON.stringify(payload)});if(!res.ok)throw new Error('analysis failed');return res.json();};
      const runApiPattern=async(payload)=>{const headers={'Content-Type':'application/json'};const url=`${settings.apiUrl.replace(/\\/$/,'')}/analysis/discussion-pattern`;const res=await fetch(url,{method:'POST',headers,body:JSON.stringify(payload)});if(!res.ok)throw new Error('pattern failed');return res.json();};

      const heuristicAnalysis=(payload)=>{const flags=[];let score=0.08;const text=payload.text||'';const lower=text.toLowerCase();if(text.length>240){score+=0.12;flags.push('長文は誤解されやすい');}if((text.match(/!{2,}/g)||[]).length>0||/[A-Z]{6,}/.test(text)){score+=0.12;flags.push('強い表現が含まれています');}const sensitiveWords=['kill','死ね','バカ','最低'];if(sensitiveWords.some(w=>lower.includes(w))){score+=0.2;flags.push('攻撃的な単語を検知');}if(payload.replying_to){score+=0.08;flags.push('返信で感情的になりやすい');}if(text.includes('http')){score+=0.05;flags.push('リンク共有は誤解リスクがあります');}score=Math.min(score,0.95);const level=score>0.45?'high':score>0.25?'medium':'low';const suggestions=['一度読み返し、感情的表現を和らげる','主語や相手を限定しない書き方にする'];if(level!=='low')suggestions.push('投稿前に5分時間を置きましょう');return{risk_level:level,risk_score:score,risk_factors:flags.length?flags:['簡易チェック結果'],suggestions};};

      const heuristicPattern=(payload)=>{const text=(payload.text||'').toLowerCase();const context=(payload.context||'').toLowerCase();const combined=`${text} ${context}`;const signals=[];if(/!!+|\\?\\?+/.test(combined))signals.push('強い感嘆や疑問符が多い');if(/(嘘|liar|fake|まちがい|間違い|not true)/.test(combined))signals.push('反論・否定が含まれる');if(/(馬鹿|ばか|stupid|idiot|crazy|dumb)/.test(combined))signals.push('攻撃的な表現');if(/(lol|草|www|lmao|sarcasm)/.test(combined))signals.push('嘲笑・皮肉が含まれる');const has=signals.length>0;const confidence=Math.min(0.85,0.35+signals.length*0.12);return{has_pattern:has,pattern_type:has?(signals[0]||'注意パターン'):'' ,confidence:has?confidence:0.1,explanation:has?`ローカル簡易判定: ${signals.join(' / ')}`:'ローカル簡易判定: 特筆なし'};};

      const callGemini=async(prompt)=>{const endpoint=`https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(settings.geminiModel)}:generateContent?key=${encodeURIComponent(settings.geminiApiKey)}`;const res=await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({contents:[{parts:[{text:prompt}]}],generationConfig:{responseMimeType:'application/json'}})});if(!res.ok)throw new Error('gemini failed');return res.json();};

      const geminiAnalysis=async(payload)=>{if(!settings.geminiApiKey){throw new Error('gemini key missing');}const prompt=`あなたはSNSの投稿監査員です。投稿文と返信先を読み、炎上や誤解のリスクを日本語で評価してください。JSONのみを返してください。形式: {"risk_level":"low|medium|high","risk_score":0-1,"risk_factors":["..."],"suggestions":["..."]}. 投稿文:${payload.text} 返信先:${payload.replying_to||'なし'}`;const data=await callGemini(prompt);const text=data?.candidates?.[0]?.content?.parts?.[0]?.text||'';try{return JSON.parse(text);}catch(_){return{risk_level:'medium',risk_score:0.32,risk_factors:['Gemini応答の解析に失敗しました'],suggestions:['JSON出力にするようプロンプトを再確認してください']};}};

      const geminiPattern=async(payload)=>{if(!settings.geminiApiKey){throw new Error('gemini key missing');}const prompt=`SNSの返信パターンを検出します。投稿文と返信先コンテキストを見て、対立を煽る/皮肉/煽り/誤情報拡散などの議論パターンがあるかを判定し、JSONのみで返してください。形式: {"has_pattern":true|false,"pattern_type":"...","confidence":0-1,"explanation":"短い説明"}. 投稿文:${payload.text} コンテキスト:${payload.context||'なし'} プラットフォーム:${payload.platform||'unknown'}`;const data=await callGemini(prompt);const text=data?.candidates?.[0]?.content?.parts?.[0]?.text||'';try{return JSON.parse(text);}catch(_){return{has_pattern:false,pattern_type:'',confidence:0.3,explanation:'Gemini応答の解析に失敗しました'};}};

      const runRiskAnalysis=async(payload)=>{
        const baseline=heuristicAnalysis(payload);
        if(!settings.enableAnalysis||settings.analysisProvider==='local'){
          return {...baseline,risk_factors:unique([...baseline.risk_factors,'API未使用:ローカル簡易チェック'])};
        }
        let advanced;
        if(settings.analysisProvider==='gemini'){
          try{advanced=await geminiAnalysis(payload);}catch(e){}
        }else{
          try{advanced=await runApiAnalysis(payload);}catch(e){}
        }
        return combineAnalysis(baseline,advanced);
      };

      const runPatternDetection=async(payload)=>{
        const baseline=heuristicPattern(payload);
        if(!settings.enablePatternDetection){
          return baseline;
        }
        if(settings.analysisProvider==='gemini'){
          try{const adv=await geminiPattern(payload);return adv||baseline;}catch(e){return baseline;}
        }
        try{const adv=await runApiPattern(payload);return adv||baseline;}catch(e){return baseline;}
      };

      const attachInterceptor=(platform,cfg)=>{const attach=()=>{const selectors=cfg.sendButtons.join(',');document.querySelectorAll(selectors).forEach((button)=>{if(button.dataset.sgBound==='true')return;button.dataset.sgBound='true';const handler=async(event)=>{if(button.dataset.sgBypass==='true')return;event.preventDefault();event.stopPropagation();const dismiss=showToast('分析中...');const steps=WORKFLOW_TEMPLATE();try{const text=getComposeText(cfg);const replyingTo=extractOriginalPost(cfg);steps[0].result=text?'完了':'入力なし';logStep('capture','入力テキストと返信先の取得',steps[0].result);const analysis=await runRiskAnalysis({text,platform,replying_to:replyingTo||undefined});steps[1].result=`${analysis.risk_level||'low'} (${Math.round((analysis.risk_score||0)*100)}%)`;logStep('analysis','リスク分析',steps[1].result);let patternResult;const hasContext=!!replyingTo;steps[2].result=hasContext?(settings.enablePatternDetection?'実行':'ローカル簡易のみ'):'スキップ (返信なし)';if(hasContext){patternResult=await runPatternDetection({text,context:replyingTo,platform});steps[2].result=patternResult?.has_pattern?`検知 (${Math.round((patternResult.confidence||0)*100)}%)`:'なし';}logStep('pattern','議論パターン検知',steps[2].result);dismiss();steps[3].result='確認中';const allow=await showModal(analysis,patternResult,steps);logStep('review','結果レビュー',allow?'送信を続行':'送信を中止');if(allow){button.dataset.sgBypass='true';button.removeEventListener('click',handler,true);button.click();setTimeout(()=>{delete button.dataset.sgBypass;button.dataset.sgBound='true';button.addEventListener('click',handler,true);},200);}}catch(error){dismiss();steps[1].result='失敗';const allow=await showModal({risk_level:'low',risk_score:0.08,risk_factors:['分析中に問題が発生しました'],suggestions:['ネットワークを確認してください','不安な場合は投稿を控えましょう']},undefined,steps);logStep('analysis','リスク分析','失敗');if(allow){button.dataset.sgBypass='true';button.removeEventListener('click',handler,true);button.click();setTimeout(()=>{delete button.dataset.sgBypass;button.dataset.sgBound='true';button.addEventListener('click',handler,true);},200);}}};button.addEventListener('click',handler,true);});};attach();const observer=new MutationObserver(attach);observer.observe(document.body,{childList:true,subtree:true});};
      const platform=detectPlatform();
      if(!platform)return;
      const cfg=PLATFORM_CONFIG[platform];
      attachInterceptor(platform,cfg);
    })());
)JS";
    return ss.str();
}

void navigate_to(AppState* state, const std::string& url) {
    std::string normalized = normalize_url(url);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(state->web_view), normalized.c_str());
}

void on_load_changed(WebKitWebView*, WebKitLoadEvent load_event, gpointer user_data) {
    if (load_event == WEBKIT_LOAD_FINISHED) {
        auto* state = static_cast<AppState*>(user_data);
        WebKitUserContentManager* manager = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(state->web_view));
        webkit_user_content_manager_remove_all_scripts(manager);
        auto script = build_guardian_script(state->settings);
        WebKitUserScript* user_script = webkit_user_script_new(
            script.c_str(),
            WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            nullptr,
            nullptr);
        webkit_user_content_manager_add_script(manager, user_script);
        g_object_unref(user_script);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    AppState state{};
    state.settings = load_settings_from_env();

    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(state.window), 1280, 900);
    gtk_window_set_title(GTK_WINDOW(state.window), "SNS Guardian Browser");

    g_signal_connect(state.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(state.window), vbox);

    GtkWidget* notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    // --- ページ1: SNSビュー ---
    GtkWidget* page_browser = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page_browser), nav_box, FALSE, FALSE, 0);

    GtkWidget* btn_x = gtk_button_new_with_label("X / Twitter");
    GtkWidget* btn_mastodon = gtk_button_new_with_label("Mastodon");
    GtkWidget* btn_bluesky = gtk_button_new_with_label("Bluesky");
    gtk_box_pack_start(GTK_BOX(nav_box), btn_x, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_box), btn_mastodon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_box), btn_bluesky, FALSE, FALSE, 0);

    state.web_view = webkit_web_view_new_with_user_content_manager(webkit_user_content_manager_new());
    gtk_widget_set_can_focus(state.web_view, TRUE);
    WebKitSettings* wk_settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(state.web_view));
    g_object_set(G_OBJECT(wk_settings),
                 "enable-smooth-scrolling", TRUE,
                 "enable-accelerated-2d-canvas", TRUE,
                 nullptr);

    gtk_box_pack_start(GTK_BOX(page_browser), state.web_view, TRUE, TRUE, 0);

    g_signal_connect(btn_x, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){ navigate_to(static_cast<AppState*>(data), "https://x.com"); }), &state);
    g_signal_connect(btn_mastodon, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){ navigate_to(static_cast<AppState*>(data), "https://mastodon.social"); }), &state);
    g_signal_connect(btn_bluesky, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){ navigate_to(static_cast<AppState*>(data), "https://bsky.app"); }), &state);

    g_signal_connect(state.web_view, "load-changed", G_CALLBACK(on_load_changed), &state);

    GtkWidget* label_browser = gtk_label_new("SNS");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_browser, label_browser);

    // --- ページ2: 設定 ---
    GtkWidget* page_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(page_settings), 6);

    GtkWidget* api_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* api_label = gtk_label_new("API URL");
    state.api_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(state.api_entry), state.settings.api_url.c_str());
    gtk_box_pack_start(GTK_BOX(api_box), api_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(api_box), state.api_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page_settings), api_box, FALSE, FALSE, 0);

    GtkWidget* provider_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* provider_label = gtk_label_new("分析プロバイダ");
    state.provider_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state.provider_combo), "api", "REST API (バックエンド経由)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state.provider_combo), "gemini", "Gemini API (直接呼び出し)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state.provider_combo), "local", "ローカル簡易チェックのみ");
    const std::string provider_id = provider_to_string(state.settings.provider);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(state.provider_combo), provider_id.c_str());
    gtk_box_pack_start(GTK_BOX(provider_box), provider_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(provider_box), state.provider_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page_settings), provider_box, FALSE, FALSE, 0);

    GtkWidget* gemini_key_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* gemini_key_label = gtk_label_new("Gemini API Key");
    state.gemini_key_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(state.gemini_key_entry), FALSE);
    gtk_entry_set_text(GTK_ENTRY(state.gemini_key_entry), state.settings.gemini_api_key.c_str());
    gtk_box_pack_start(GTK_BOX(gemini_key_box), gemini_key_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gemini_key_box), state.gemini_key_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page_settings), gemini_key_box, FALSE, FALSE, 0);

    GtkWidget* gemini_model_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* gemini_model_label = gtk_label_new("Gemini Model");
    state.gemini_model_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(state.gemini_model_entry), state.settings.gemini_model.c_str());
    gtk_box_pack_start(GTK_BOX(gemini_model_box), gemini_model_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gemini_model_box), state.gemini_model_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page_settings), gemini_model_box, FALSE, FALSE, 0);

    GtkWidget* env_note = gtk_label_new("環境変数 SNS_GUARDIAN_* で初期値を指定できます。");
    gtk_label_set_xalign(GTK_LABEL(env_note), 0.0);
    gtk_box_pack_start(GTK_BOX(page_settings), env_note, FALSE, FALSE, 0);

    GtkWidget* local_note = gtk_label_new("バックエンドの別起動は不要です。ローカル簡易チェックは常に実行されます。");
    gtk_label_set_xalign(GTK_LABEL(local_note), 0.0);
    gtk_box_pack_start(GTK_BOX(page_settings), local_note, FALSE, FALSE, 0);

    state.toggle_analysis = gtk_check_button_new_with_label("高度リスク分析 (API/Gemini) を使う");
    state.toggle_pattern = gtk_check_button_new_with_label("高度パターン検知 (API/Gemini) を使う");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.toggle_analysis), state.settings.enable_analysis);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.toggle_pattern), state.settings.enable_pattern);
    gtk_box_pack_start(GTK_BOX(page_settings), state.toggle_analysis, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page_settings), state.toggle_pattern, FALSE, FALSE, 0);

    GtkWidget* apply_btn = gtk_button_new_with_label("設定を適用");
    gtk_box_pack_start(GTK_BOX(page_settings), apply_btn, FALSE, FALSE, 0);

    g_signal_connect(apply_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){
        auto* st = static_cast<AppState*>(data);
        const char* api = gtk_entry_get_text(GTK_ENTRY(st->api_entry));
        st->settings.api_url = api ? api : "";
        const char* provider_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(st->provider_combo));
        st->settings.provider = string_to_provider(provider_id ? provider_id : "api");
        const char* gemini_key = gtk_entry_get_text(GTK_ENTRY(st->gemini_key_entry));
        st->settings.gemini_api_key = gemini_key ? gemini_key : "";
        const char* gemini_model = gtk_entry_get_text(GTK_ENTRY(st->gemini_model_entry));
        st->settings.gemini_model = (gemini_model && *gemini_model) ? gemini_model : "gemini-1.5-flash-latest";
        st->settings.enable_analysis = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->toggle_analysis));
        st->settings.enable_pattern = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->toggle_pattern));

        // 再度スクリプトを差し替え
        WebKitUserContentManager* manager = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(st->web_view));
        webkit_user_content_manager_remove_all_scripts(manager);
        auto script = build_guardian_script(st->settings);
        WebKitUserScript* user_script = webkit_user_script_new(
            script.c_str(),
            WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            nullptr,
            nullptr);
        webkit_user_content_manager_add_script(manager, user_script);
        g_object_unref(user_script);
    }), &state);

    GtkWidget* label_settings = gtk_label_new("設定");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_settings, label_settings);

    gtk_widget_show_all(state.window);

    // 初回ロードはXへ
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(state.web_view), "https://x.com");
    gtk_widget_grab_focus(state.web_view);

    // Ctrl+V での貼り付けをサポート（WebView内部でIME/キーボード入力が効くようフォーカスを保持）
    g_signal_connect(state.window, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        auto* st = static_cast<AppState*>(data);
        if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V)) {
            webkit_web_view_execute_editing_command(WEBKIT_WEB_VIEW(st->web_view), WEBKIT_EDITING_COMMAND_PASTE);
            return TRUE;
        }
        // フォーカスが外れている場合はWebViewに戻す
        if (!gtk_widget_has_focus(st->web_view)) {
            gtk_widget_grab_focus(st->web_view);
        }
        return FALSE;
    }), &state);

    gtk_main();
    return 0;
}
