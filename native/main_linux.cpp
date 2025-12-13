#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <curl/curl.h>
#include <thread>
#include <mutex>

namespace {

enum class AnalysisProvider {
    Api,
    Gemini,
    LocalHeuristic
};

struct GuardianSettings {
    std::string api_url = "http://localhost:8000/api/v1";
    std::string gemini_api_key{};
    std::string gemini_model = "gemini-2.5-flash-lite-preview-09-2025";
    AnalysisProvider provider = AnalysisProvider::LocalHeuristic;
    bool enable_analysis = true;
    bool enable_pattern = true;
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
    GtkWidget* notebook = nullptr;
    GuardianSettings settings{};
};

std::string js_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\'': out += "\\'"; break;
        case '`': out += "\\`"; break;
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
    if (const char* api = std::getenv("SNS_GUARDIAN_API_URL")) settings.api_url = api;
    if (const char* provider = std::getenv("SNS_GUARDIAN_PROVIDER")) settings.provider = string_to_provider(provider);
    settings.enable_analysis = parse_bool_env(std::getenv("SNS_GUARDIAN_ENABLE_ANALYSIS"), settings.enable_analysis);
    settings.enable_pattern = parse_bool_env(std::getenv("SNS_GUARDIAN_ENABLE_PATTERN"), settings.enable_pattern);
    if (const char* key = std::getenv("SNS_GUARDIAN_GEMINI_API_KEY")) settings.gemini_api_key = key;
    if (const char* model = std::getenv("SNS_GUARDIAN_GEMINI_MODEL")) settings.gemini_model = model;
    return settings;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}

std::string perform_gemini_request(const std::string& api_key, const std::string& model, const std::string& text) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    
    g_print("[SNS Guardian C++] perform_gemini_request called\n");
    g_print("[SNS Guardian C++] Model: %s\n", model.c_str());
    g_print("[SNS Guardian C++] API Key length: %zu\n", api_key.length());

    curl = curl_easy_init();
    if(curl) {
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent?key=" + api_key;
        
        std::string escaped_text = js_escape(text);
        std::string payload = R"({"contents":[{"parts":[{"text":"SNS投稿のリスク分析をしてください。JSONのみを返してください。形式: {\"risk_level\":\"low|medium|high\",\"risk_score\":0-1,\"risk_factors\":[\"...\"],\"suggestions\":[\"...\"]}. 投稿文: )" + escaped_text + R"("}]}],"generationConfig":{"responseMimeType":"application/json"}})";

        g_print("[SNS Guardian C++] URL: %s\n", url.substr(0, 80).c_str());

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            g_print("[SNS Guardian C++] CURL error: %s\n", curl_easy_strerror(res));
            readBuffer = "{\"error\": \"CURL error: " + std::string(curl_easy_strerror(res)) + "\"}";
        } else {
            g_print("[SNS Guardian C++] Response received, length: %zu\n", readBuffer.length());
        }
        
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return readBuffer;
}

std::string extract_gemini_text(const std::string& json) {
    std::string key = "\"text\":";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos += key.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    if (pos >= json.length()) return "";
    pos--; // back to quote
    if (json[pos] != '"') return "";
    pos++;
    
    std::string result;
    bool escaped = false;
    for (size_t i = pos; i < json.length(); ++i) {
        char c = json[i];
        if (escaped) {
            if (c == '"') result += '"';
            else if (c == '\\') result += '\\';
            else if (c == 'n') result += '\n';
            else result += c;
            escaped = false;
        } else {
            if (c == '\\') escaped = true;
            else if (c == '"') break;
            else result += c;
        }
    }
    g_print("[SNS Guardian C++] Extracted text: %s\n", result.substr(0, 100).c_str());
    return result;
}

std::string normalize_url(const std::string& input) {
    std::string trimmed = input;
    while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    if (trimmed.empty()) return "https://x.com";
    if (trimmed.rfind("http://", 0) == 0 || trimmed.rfind("https://", 0) == 0) return trimmed;
    return "https://" + trimmed;
}

void navigate_to(AppState* state, const std::string& url) {
    std::string normalized = normalize_url(url);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(state->web_view), normalized.c_str());
}

void on_load_changed(WebKitWebView* web_view, WebKitLoadEvent load_event, gpointer user_data) {
    if (load_event == WEBKIT_LOAD_FINISHED) {
        auto* state = static_cast<AppState*>(user_data);
        
        g_print("\n[SNS Guardian] === Page Load Complete ===\n");
        g_print("[SNS Guardian] Provider: %s\n", provider_to_string(state->settings.provider).c_str());
        g_print("[SNS Guardian] API Key set: %s\n", state->settings.gemini_api_key.empty() ? "NO" : "YES");
        g_print("[SNS Guardian] Model: %s\n", state->settings.gemini_model.c_str());
        g_print("[SNS Guardian] Enable Analysis: %s\n", state->settings.enable_analysis ? "true" : "false");
        
        std::ostringstream script;
        script << R"JS(
(function() {
    console.log('[SNS Guardian] Script starting...');
    
    var settings = {
        apiUrl: ')JS" << js_escape(state->settings.api_url) << R"JS(',
        provider: ')JS" << provider_to_string(state->settings.provider) << R"JS(',
        geminiApiKey: ')JS" << js_escape(state->settings.gemini_api_key) << R"JS(',
        geminiModel: ')JS" << js_escape(state->settings.gemini_model) << R"JS(',
        enableAnalysis: )JS" << (state->settings.enable_analysis ? "true" : "false") << R"JS(,
        enablePattern: )JS" << (state->settings.enable_pattern ? "true" : "false") << R"JS(
    };
    
    console.log('[SNS Guardian] Settings loaded:', settings.provider, 'apiKey:', settings.geminiApiKey ? 'SET' : 'NOT SET');
    
    var h = location.hostname;
    var platform = null;
    if(h.includes('twitter.com') || h.includes('x.com')) platform = 'x';
    else if(h.includes('mastodon')) platform = 'mastodon';
    else if(h.includes('bsky.app')) platform = 'bluesky';
    
    console.log('[SNS Guardian] Platform:', platform);
    if(!platform) return;
    
    var sensitiveWords = ['kill', '死ね', 'バカ', '最低', '馬鹿', 'ばか', 'stupid', 'idiot'];
    
    function localAnalysis(text) {
        console.log('[SNS Guardian] Local analysis...');
        var score = 0.08;
        var factors = [];
        var lower = text.toLowerCase();
        
        if(text.length > 240) { score += 0.12; factors.push('長文は誤解されやすい'); }
        if(/!{2,}/.test(text) || /[A-Z]{6,}/.test(text)) { score += 0.12; factors.push('強い表現が含まれています'); }
        
        for(var i = 0; i < sensitiveWords.length; i++) {
            if(lower.includes(sensitiveWords[i].toLowerCase())) {
                score += 0.2;
                factors.push('攻撃的な単語を検知');
                break;
            }
        }
        
        if(text.includes('http')) { score += 0.05; factors.push('リンク共有'); }
        
        score = Math.min(score, 0.95);
        var level = score > 0.45 ? 'high' : score > 0.25 ? 'medium' : 'low';
        
        return { level: level, score: score, factors: factors };
    }
    
    var lastGeminiError = '';
    
    async function geminiAnalysis(text) {
        console.log('[SNS Guardian] Starting Gemini analysis...');
        lastGeminiError = '';
        
        if(!settings.geminiApiKey) {
            lastGeminiError = 'API key not set';
            console.log('[SNS Guardian] Error:', lastGeminiError);
            return null;
        }
        
        if (!window.webkit || !window.webkit.messageHandlers || !window.webkit.messageHandlers.gemini) {
            lastGeminiError = 'Native handler not available';
            console.log('[SNS Guardian] Error:', lastGeminiError);
            return null;
        }
        
        console.log('[SNS Guardian] Sending to native handler...');
        
        return new Promise(function(resolve) {
            var timeoutId = setTimeout(function() {
                lastGeminiError = 'Timeout';
                console.log('[SNS Guardian] Gemini timeout');
                resolve(null);
            }, 15000);
            
            window.geminiCallback = function(jsonStr) {
                console.log('[SNS Guardian] Callback received:', jsonStr ? jsonStr.substring(0, 100) : 'empty');
                clearTimeout(timeoutId);
                
                if(!jsonStr) {
                    lastGeminiError = 'Empty response';
                    resolve(null);
                    return;
                }
                
                try {
                    var analysis = JSON.parse(jsonStr);
                    console.log('[SNS Guardian] Parsed analysis:', analysis);
                    
                    // APIエラーをチェック（429 quota exceededなど）
                    if(analysis.error) {
                        var errCode = analysis.error.code || 'unknown';
                        var errMsg = analysis.error.message || 'Unknown error';
                        if(errCode === 429) {
                            lastGeminiError = 'API quota exceeded (429)';
                        } else {
                            lastGeminiError = 'API error ' + errCode + ': ' + errMsg.substring(0, 50);
                        }
                        console.log('[SNS Guardian] API error detected:', lastGeminiError);
                        resolve(null);
                        return;
                    }
                    
                    resolve(analysis);
                } catch(e) {
                    lastGeminiError = 'Parse error';
                    console.log('[SNS Guardian] Parse error:', e.message, jsonStr.substring(0, 50));
                    resolve(null);
                }
            };
            
            try {
                window.webkit.messageHandlers.gemini.postMessage(text);
            } catch(e) {
                clearTimeout(timeoutId);
                lastGeminiError = 'PostMessage failed';
                console.log('[SNS Guardian] PostMessage error:', e);
                resolve(null);
            }
        });
    }
    
    async function analyzeRisk(text) {
        console.log('[SNS Guardian] analyzeRisk, provider:', settings.provider);
        var local = localAnalysis(text);
        var usedProvider = 'local';
        
        if(!settings.enableAnalysis || settings.provider === 'local') {
            local.usedProvider = 'local';
            return local;
        }
        
        if(settings.provider === 'gemini') {
            console.log('[SNS Guardian] Calling Gemini...');
            var advanced = await geminiAnalysis(text);
            
            if(advanced && advanced.risk_level) {
                console.log('[SNS Guardian] Using Gemini result');
                return {
                    level: advanced.risk_level,
                    score: advanced.risk_score || local.score,
                    factors: (advanced.risk_factors || []).concat(local.factors),
                    suggestions: advanced.suggestions || [],
                    usedProvider: 'gemini'
                };
            } else {
                console.log('[SNS Guardian] Gemini failed, using local. Error:', lastGeminiError);
                local.usedProvider = 'gemini (failed: ' + lastGeminiError + ')';
            }
        }
        
        return local;
    }
    
    function showModal(analysis, onContinue, onCancel) {
        var overlay = document.createElement('div');
        overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.6);display:flex;align-items:center;justify-content:center;z-index:2147483647;';
        
        var riskColor = analysis.level === 'high' ? '#ef4444' : analysis.level === 'medium' ? '#f59e0b' : '#22c55e';
        var riskPercent = Math.round(analysis.score * 100);
        
        var modal = document.createElement('div');
        modal.style.cssText = 'background:#fff;border-radius:12px;padding:20px;max-width:400px;width:90%;font-family:sans-serif;';
        modal.innerHTML = '<h3 style="margin:0 0 16px;color:#0f172a;">送信前チェック</h3>' +
            '<div style="background:#f1f5f9;padding:12px;border-radius:8px;margin-bottom:12px;">' +
            '<div style="font-size:14px;color:#64748b;">リスクスコア</div>' +
            '<div style="font-size:24px;font-weight:bold;color:' + riskColor + ';">' + riskPercent + '% (' + analysis.level + ')</div>' +
            '<div style="font-size:11px;color:#94a3b8;margin-top:4px;">分析: ' + (analysis.usedProvider || 'unknown') + '</div>' +
            '</div>' +
            '<div style="margin-bottom:16px;">' +
            '<div style="font-size:14px;font-weight:bold;color:#0f172a;margin-bottom:8px;">検出された要因:</div>' +
            '<ul style="margin:0;padding-left:20px;color:#334155;">' + 
            (analysis.factors && analysis.factors.length > 0 ? analysis.factors.map(function(f){ return '<li>' + f + '</li>'; }).join('') : '<li>特になし</li>') +
            '</ul></div>' +
            '<div style="display:flex;gap:8px;justify-content:flex-end;">' +
            '<button id="sg-cancel" style="padding:10px 16px;border:1px solid #e2e8f0;background:#fff;border-radius:8px;cursor:pointer;font-weight:bold;">投稿を中止</button>' +
            '<button id="sg-continue" style="padding:10px 16px;border:none;background:#2563eb;color:#fff;border-radius:8px;cursor:pointer;font-weight:bold;">それでも投稿</button></div>';
        
        overlay.appendChild(modal);
        document.body.appendChild(overlay);
        
        modal.querySelector('#sg-cancel').onclick = function() { overlay.remove(); onCancel(); };
        modal.querySelector('#sg-continue').onclick = function() { overlay.remove(); onContinue(); };
    }
    
    var buttonSelectors = platform === 'x' ? 
        'button[data-testid="tweetButtonInline"],button[data-testid="tweetButton"],div[data-testid="tweetButtonInline"],div[data-testid="tweetButton"]' :
        platform === 'mastodon' ? 'button[type="submit"]' : 'button[data-testid="composer-submit"]';
    
    var textSelectors = platform === 'x' ?
        'div[data-testid="tweetTextarea_0"],div[role="textbox"][contenteditable="true"]' :
        platform === 'mastodon' ? 'textarea' : 'textarea,div[role="textbox"]';
    
    var isUpdating = false;
    
    function attachToButtons() {
        if(isUpdating) return;
        isUpdating = true;
        
        var buttons = document.querySelectorAll(buttonSelectors);
        
        buttons.forEach(function(btn) {
            if(btn.dataset.sgBound === 'true') return;
            btn.dataset.sgBound = 'true';
            console.log('[SNS Guardian] Attached to button');
            
            btn.addEventListener('click', async function(e) {
                if(btn.dataset.sgBypass === 'true') return;
                
                e.preventDefault();
                e.stopPropagation();
                
                var textEl = document.querySelector(textSelectors);
                var text = textEl ? (textEl.textContent || textEl.value || '') : '';
                console.log('[SNS Guardian] Intercepted, text:', text.substring(0, 30));
                
                var analysis = await analyzeRisk(text);
                
                showModal(analysis, 
                    function() {
                        btn.dataset.sgBypass = 'true';
                        btn.click();
                        setTimeout(function() { btn.dataset.sgBypass = 'false'; }, 500);
                    },
                    function() {}
                );
            }, true);
        });
        
        isUpdating = false;
    }
    
    attachToButtons();
    
    var debounceTimer = null;
    var observer = new MutationObserver(function() {
        if(debounceTimer) clearTimeout(debounceTimer);
        debounceTimer = setTimeout(attachToButtons, 500);
    });
    observer.observe(document.body, { childList: true, subtree: true });
    
    console.log('[SNS Guardian] Initialization complete');
})();
)JS";
        
        webkit_web_view_evaluate_javascript(
            WEBKIT_WEB_VIEW(state->web_view),
            script.str().c_str(),
            -1, nullptr, nullptr, nullptr, nullptr, nullptr
        );
    }
}

} // namespace

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    AppState state;
    state.settings = load_settings_from_env();

    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "SNS Guardian Browser");
    gtk_window_set_default_size(GTK_WINDOW(state.window), 1200, 800);

    g_signal_connect(state.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    // CSS
    GtkCssProvider* css_provider = gtk_css_provider_new();
    const char* css = R"CSS(
        * { -gtk-icon-style: symbolic; }
        window, .background { background-color: #0a0a0f; color: #ddddee; }
        box, scrolledwindow, viewport { background-color: transparent; }
        button { background-color: #1a1a2e; background-image: none; border: 2px solid #4a4a6a; border-radius: 8px; color: #ddddee; padding: 8px 16px; }
        button:hover { background-color: #2a2a4a; border-color: #00fff2; color: #00fff2; }
        .nav-button { background-color: #1a1a2e; background-image: none; border: 2px solid #4a4a6a; border-radius: 8px; color: #ddddee; font-weight: 600; min-height: 32px; }
        .nav-button:hover { background-color: #2a2a4a; border-color: #00fff2; color: #00fff2; }
        notebook { background-color: #0a0a0f; }
        notebook header { background-color: #12121a; border-bottom: 2px solid #00fff2; }
        notebook header tab { background-color: #1a1a2e; background-image: none; color: #888899; padding: 12px 24px; border-radius: 8px 8px 0 0; border: 1px solid #2a2a4a; font-weight: bold; }
        notebook header tab:checked { background-color: #00fff2; background-image: none; color: #0a0a0f; }
        notebook > stack { background-color: #0a0a0f; }
        .settings-page { background-color: #0a0a0f; padding: 16px; }
        .settings-card { background-color: #12121a; border: 2px solid #00fff2; border-radius: 12px; padding: 16px; margin: 6px 0; }
        .section-title { color: #00fff2; font-size: 15px; font-weight: bold; }
        label, .settings-label { color: #ddddee; background-color: transparent; }
        entry { background-color: #1a1a2e; background-image: none; border: 2px solid #4a4a6a; border-radius: 6px; padding: 8px; color: #ffffff; min-height: 16px; }
        entry:focus { border-color: #ff00ff; }
        combobox, combobox * { background-color: #1a1a2e; color: #ffffff; }
        combobox button { background-color: #1a1a2e; background-image: none; border: 2px solid #4a4a6a; border-radius: 6px; color: #ffffff; }
        combobox button:hover { border-color: #00fff2; background-color: #2a2a4a; }
        combobox cellview { background-color: transparent; color: #ffffff; }
        menu, popover { background-color: #1a1a2e; border: 2px solid #00fff2; border-radius: 8px; }
        menuitem { background-color: #1a1a2e; color: #ffffff; padding: 8px 12px; }
        menuitem:hover { background-color: #00fff2; color: #0a0a0f; }
        checkbutton { color: #ddddee; }
        checkbutton check { background-color: #1a1a2e; background-image: none; border: 2px solid #4a4a6a; border-radius: 4px; }
        checkbutton:checked check { background-color: #ff00ff; border-color: #ff00ff; }
        .apply-button { background-image: linear-gradient(135deg, #ff00ff, #00fff2); background-color: #ff00ff; border: none; border-radius: 8px; padding: 12px 28px; color: #ffffff; font-weight: bold; min-height: 40px; }
        .apply-button:hover { background-image: linear-gradient(135deg, #ff44ff, #44ffff); }
    )CSS";
    
    gtk_css_provider_load_from_data(css_provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(state.window), vbox);

    GtkWidget* notebook = gtk_notebook_new();
    state.notebook = notebook;
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    // Page 1: Browser
    GtkWidget* page_browser = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page_browser), nav_box, FALSE, FALSE, 0);

    GtkWidget* btn_x = gtk_button_new_with_label("X / Twitter");
    GtkWidget* btn_mastodon = gtk_button_new_with_label("Mastodon");
    GtkWidget* btn_bluesky = gtk_button_new_with_label("Bluesky");
    
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_x), "nav-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_mastodon), "nav-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_bluesky), "nav-button");

    gtk_box_pack_start(GTK_BOX(nav_box), btn_x, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_box), btn_mastodon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_box), btn_bluesky, FALSE, FALSE, 0);

    // Session persistence
    std::string data_dir = std::string(g_get_home_dir()) + "/.sns_guardian_browser";
    WebKitWebsiteDataManager* data_manager = webkit_website_data_manager_new(
        "base-data-directory", data_dir.c_str(),
        "base-cache-directory", (data_dir + "/cache").c_str(),
        nullptr);
    
    WebKitCookieManager* cookie_manager = webkit_website_data_manager_get_cookie_manager(data_manager);
    std::string cookie_file = data_dir + "/cookies.txt";
    webkit_cookie_manager_set_persistent_storage(cookie_manager, cookie_file.c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
    webkit_cookie_manager_set_accept_policy(cookie_manager, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    
    WebKitWebContext* web_context = webkit_web_context_new_with_website_data_manager(data_manager);
    WebKitUserContentManager* content_manager = webkit_user_content_manager_new();
    
    // Gemini message handler
    webkit_user_content_manager_register_script_message_handler(content_manager, "gemini");
    g_signal_connect(content_manager, "script-message-received::gemini", G_CALLBACK(+[](WebKitUserContentManager*, WebKitJavascriptResult* js_result, gpointer data) {
        auto* st = static_cast<AppState*>(data);
        JSCValue* value = webkit_javascript_result_get_js_value(js_result);
        if (jsc_value_is_string(value)) {
            char* text_c = jsc_value_to_string(value);
            std::string text = text_c;
            g_free(text_c);
            
            g_print("[SNS Guardian C++] Received message from JS, length: %zu\n", text.length());
            
            std::thread([st, text]() {
                std::string result_json = perform_gemini_request(st->settings.gemini_api_key, st->settings.gemini_model, text);
                std::string content = extract_gemini_text(result_json);
                if (content.empty()) content = result_json;
                
                g_idle_add(+[](gpointer user_data) -> gboolean {
                    auto* params = static_cast<std::pair<AppState*, std::string>*>(user_data);
                    std::string callback_js = "if(window.geminiCallback) window.geminiCallback(`" + js_escape(params->second) + "`);";
                    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(params->first->web_view), callback_js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
                    delete params;
                    return FALSE;
                }, new std::pair<AppState*, std::string>(st, content));
            }).detach();
        }
    }), &state);

    state.web_view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "web-context", web_context,
        "user-content-manager", content_manager,
        nullptr));
    
    gtk_widget_set_can_focus(state.web_view, TRUE);
    WebKitSettings* wk_settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(state.web_view));
    g_object_set(G_OBJECT(wk_settings), "enable-developer-extras", TRUE, nullptr);

    gtk_box_pack_start(GTK_BOX(page_browser), state.web_view, TRUE, TRUE, 0);

    g_signal_connect(btn_x, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){ navigate_to(static_cast<AppState*>(data), "https://x.com"); }), &state);
    g_signal_connect(btn_mastodon, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){ navigate_to(static_cast<AppState*>(data), "https://mastodon.social"); }), &state);
    g_signal_connect(btn_bluesky, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){ navigate_to(static_cast<AppState*>(data), "https://bsky.app"); }), &state);

    g_signal_connect(state.web_view, "load-changed", G_CALLBACK(on_load_changed), &state);

    GtkWidget* label_browser = gtk_label_new("SNS");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_browser, label_browser);

    // Page 2: Settings
    GtkWidget* page_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(page_settings), "settings-page");
    gtk_container_set_border_width(GTK_CONTAINER(page_settings), 16);
    
    GtkWidget* title_label = gtk_label_new("SNS GUARDIAN 設定");
    gtk_style_context_add_class(gtk_widget_get_style_context(title_label), "section-title");
    PangoAttrList* title_attrs = pango_attr_list_new();
    pango_attr_list_insert(title_attrs, pango_attr_scale_new(2.0));
    pango_attr_list_insert(title_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title_label), title_attrs);
    pango_attr_list_unref(title_attrs);
    gtk_widget_set_halign(title_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(page_settings), title_label, FALSE, FALSE, 8);
    
    GtkWidget* main_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(main_card), "settings-card");
    
    // Provider
    GtkWidget* provider_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* provider_label = gtk_label_new("プロバイダ:");
    gtk_widget_set_size_request(provider_label, 100, -1);
    state.provider_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state.provider_combo), "local", "ローカル");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state.provider_combo), "gemini", "Gemini API");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state.provider_combo), "api", "REST API");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(state.provider_combo), provider_to_string(state.settings.provider).c_str());
    gtk_box_pack_start(GTK_BOX(provider_row), provider_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(provider_row), state.provider_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_card), provider_row, FALSE, FALSE, 0);
    
    // API URL
    GtkWidget* api_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* api_label = gtk_label_new("API URL:");
    gtk_widget_set_size_request(api_label, 100, -1);
    state.api_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(state.api_entry), state.settings.api_url.c_str());
    gtk_box_pack_start(GTK_BOX(api_row), api_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(api_row), state.api_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_card), api_row, FALSE, FALSE, 0);
    
    // Gemini API Key
    GtkWidget* key_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* key_label = gtk_label_new("API Key:");
    gtk_widget_set_size_request(key_label, 100, -1);
    state.gemini_key_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(state.gemini_key_entry), FALSE);
    gtk_entry_set_text(GTK_ENTRY(state.gemini_key_entry), state.settings.gemini_api_key.c_str());
    gtk_box_pack_start(GTK_BOX(key_row), key_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(key_row), state.gemini_key_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_card), key_row, FALSE, FALSE, 0);
    
    // Gemini Model
    GtkWidget* model_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* model_label = gtk_label_new("Model:");
    gtk_widget_set_size_request(model_label, 100, -1);
    state.gemini_model_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(state.gemini_model_entry), state.settings.gemini_model.c_str());
    gtk_box_pack_start(GTK_BOX(model_row), model_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(model_row), state.gemini_model_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_card), model_row, FALSE, FALSE, 0);
    
    // Checkboxes
    GtkWidget* check_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(check_row, GTK_ALIGN_CENTER);
    state.toggle_analysis = gtk_check_button_new_with_label("高度分析");
    state.toggle_pattern = gtk_check_button_new_with_label("パターン検知");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.toggle_analysis), state.settings.enable_analysis);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.toggle_pattern), state.settings.enable_pattern);
    gtk_box_pack_start(GTK_BOX(check_row), state.toggle_analysis, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(check_row), state.toggle_pattern, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_card), check_row, FALSE, FALSE, 8);
    
    gtk_box_pack_start(GTK_BOX(page_settings), main_card, FALSE, FALSE, 0);
    
    // Apply button
    GtkWidget* apply_btn = gtk_button_new_with_label("設定を適用");
    gtk_style_context_add_class(gtk_widget_get_style_context(apply_btn), "apply-button");
    gtk_widget_set_size_request(apply_btn, 250, 50);
    gtk_widget_set_halign(apply_btn, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(page_settings), apply_btn, FALSE, FALSE, 16);

    g_signal_connect(apply_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){
        auto* st = static_cast<AppState*>(data);
        
        const char* api = gtk_entry_get_text(GTK_ENTRY(st->api_entry));
        st->settings.api_url = api ? api : "";
        
        const char* provider_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(st->provider_combo));
        st->settings.provider = string_to_provider(provider_id ? provider_id : "local");
        
        const char* gemini_key = gtk_entry_get_text(GTK_ENTRY(st->gemini_key_entry));
        st->settings.gemini_api_key = gemini_key ? gemini_key : "";
        
        const char* gemini_model = gtk_entry_get_text(GTK_ENTRY(st->gemini_model_entry));
        st->settings.gemini_model = (gemini_model && *gemini_model) ? gemini_model : "gemini-2.5-flash-lite-preview-09-2025";
        
        st->settings.enable_analysis = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->toggle_analysis));
        st->settings.enable_pattern = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->toggle_pattern));

        g_print("\n[SNS Guardian] Settings applied:\n");
        g_print("  Provider: %s\n", provider_to_string(st->settings.provider).c_str());
        g_print("  API Key: %s\n", st->settings.gemini_api_key.empty() ? "(not set)" : "(set)");
        g_print("  Model: %s\n", st->settings.gemini_model.c_str());

        // beforeunloadを無効化してから強制リロード
        const char* disable_beforeunload = "window.onbeforeunload = null; window.addEventListener('beforeunload', function(e) { e.stopImmediatePropagation(); }, true);";
        webkit_web_view_evaluate_javascript(
            WEBKIT_WEB_VIEW(st->web_view),
            disable_beforeunload,
            -1, nullptr, nullptr, nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer user_data) {
                auto* web_view = WEBKIT_WEB_VIEW(source);
                webkit_web_view_evaluate_javascript_finish(web_view, result, nullptr);
                webkit_web_view_reload_bypass_cache(web_view);
            },
            nullptr
        );
        
        if(st->notebook) gtk_notebook_set_current_page(GTK_NOTEBOOK(st->notebook), 0);
    }), &state);

    GtkWidget* label_settings = gtk_label_new("設定");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_settings, label_settings);

    gtk_widget_show_all(state.window);

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(state.web_view), "https://x.com");
    gtk_widget_grab_focus(state.web_view);

    // Ctrl+V support
    g_signal_connect(state.window, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        auto* st = static_cast<AppState*>(data);
        if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V)) {
            GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            gchar* text = gtk_clipboard_wait_for_text(clipboard);
            if (text && strlen(text) > 0) {
                std::string escaped;
                for (const char* p = text; *p; ++p) {
                    if (*p == '\\') escaped += "\\\\";
                    else if (*p == '\'') escaped += "\\'";
                    else if (*p == '\n') escaped += "\\n";
                    else if (*p == '\r') continue;
                    else escaped += *p;
                }
                std::string js = "document.execCommand('insertText', false, '" + escaped + "');";
                webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(st->web_view), js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
                g_free(text);
                return TRUE;
            }
            if (text) g_free(text);
        }
        return FALSE;
    }), &state);

    gtk_main();
    return 0;
}
