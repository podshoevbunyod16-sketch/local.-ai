#include "llama-tools.h"
#include <cstdio>
#include <ctime>
#include <sstream>
#include <fstream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cctype>
#include <iostream>

#ifdef __ANDROID__
#define CURL_PATH "/data/data/com.termux/files/usr/bin/curl"
#else
#define CURL_PATH "curl"
#endif
namespace llama_tools {

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==========

static std::string exec(const char* cmd, int timeout_sec) {
    char buffer[65536];
    std::string result;
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "ERROR";
    time_t start = time(NULL);
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
        if (difftime(time(NULL), start) > timeout_sec) break;
    }
    pclose(pipe);
    return result;
}

static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    for (size_t i = 0; i < value.length(); i++) {
        unsigned char c = value[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::hex << (int)c << std::nouppercase;
        }
    }
    return escaped.str();
}

static std::string strip_html(const std::string& html) {
    std::string text = html;
    size_t pos = 0;
    
    while ((pos = text.find("<script")) != std::string::npos) {
        size_t end = text.find("</script>", pos);
        if (end == std::string::npos) break;
        text.replace(pos, end - pos + 9, " ");
    }
    
    while ((pos = text.find("<style")) != std::string::npos) {
        size_t end = text.find("</style>", pos);
        if (end == std::string::npos) break;
        text.replace(pos, end - pos + 8, " ");
    }
    
    while ((pos = text.find("<nav")) != std::string::npos) {
        size_t end = text.find("</nav>", pos);
        if (end == std::string::npos) break;
        text.replace(pos, end - pos + 6, " ");
    }
    
    while ((pos = text.find("<footer")) != std::string::npos) {
        size_t end = text.find("</footer>", pos);
        if (end == std::string::npos) break;
        text.replace(pos, end - pos + 9, " ");
    }
    
    std::vector<std::string> block_tags;
    block_tags.push_back("<br");
    block_tags.push_back("<p");
    block_tags.push_back("<div");
    block_tags.push_back("<h1");
    block_tags.push_back("<h2");
    block_tags.push_back("<h3");
    block_tags.push_back("<h4");
    block_tags.push_back("<h5");
    block_tags.push_back("<h6");
    block_tags.push_back("<li");
    block_tags.push_back("<tr");
    block_tags.push_back("<td");
    
    for (size_t t = 0; t < block_tags.size(); t++) {
        pos = 0;
        while ((pos = text.find(block_tags[t], pos)) != std::string::npos) {
            size_t end = text.find(">", pos);
            if (end == std::string::npos) break;
            text.replace(pos, end - pos + 1, "\n");
        }
    }
    
    while ((pos = text.find("<")) != std::string::npos) {
        size_t end = text.find(">", pos);
        if (end == std::string::npos) break;
        text.replace(pos, end - pos + 1, " ");
    }
    
    std::vector<std::pair<std::string, std::string> > entities;
    entities.push_back(std::make_pair("&quot;", "\""));
    entities.push_back(std::make_pair("&amp;", "&"));
    entities.push_back(std::make_pair("&lt;", "<"));
    entities.push_back(std::make_pair("&gt;", ">"));
    entities.push_back(std::make_pair("&nbsp;", " "));
    entities.push_back(std::make_pair("&#39;", "'"));
    entities.push_back(std::make_pair("&apos;", "'"));
    entities.push_back(std::make_pair("&ndash;", "-"));
    entities.push_back(std::make_pair("&mdash;", "-"));
    entities.push_back(std::make_pair("&laquo;", "\""));
    entities.push_back(std::make_pair("&raquo;", "\""));
    entities.push_back(std::make_pair("&#x27;", "'"));
    
    for (size_t e = 0; e < entities.size(); e++) {
        pos = 0;
        while ((pos = text.find(entities[e].first, pos)) != std::string::npos) {
            text.replace(pos, entities[e].first.length(), entities[e].second);
        }
    }
    
    while ((pos = text.find("  ")) != std::string::npos) {
        text.replace(pos, 2, " ");
    }
    while ((pos = text.find("\n\n")) != std::string::npos) {
        text.replace(pos, 2, "\n");
    }
    
    while (text.length() > 0 && text[0] == ' ') text = text.substr(1);
    while (text.length() > 0 && text[text.length()-1] == ' ') text = text.substr(0, text.length()-1);
    
    return text;
}

static int score_content(const std::string& text) {
    int score = 0;
    int len = (int)text.length();
    score += len / 100;
    if (score > 50) score = 50;
    
    int sentences = 0;
    for (size_t i = 0; i < text.length(); i++) {
        if (text[i] == '.' || text[i] == '!' || text[i] == '?') sentences++;
    }
    score += sentences * 2;
    
    int digits = 0;
    for (size_t i = 0; i < text.length(); i++) {
        if (isdigit(text[i])) digits++;
    }
    score += digits;
    
    if (len < 200) score -= 50;
    if (text.find("http://") != std::string::npos) score -= 10;
    if (text.find("https://") != std::string::npos) score -= 10;
    
    return score;
}

// ========== HTTP И API ФУНКЦИИ ==========

std::string http_get(const std::string& url) {
    std::string cmd = std::string(CURL_PATH) + 
        " -s -L --max-time 30" +
        " -H \"User-Agent: Mozilla/5.0\"" +
        " -H \"Accept: */*\"" +
        " \"" + url + "\" 2>/dev/null";
    return exec(cmd.c_str(), 30);
}

static std::string hf_api_post(const std::string& endpoint, const std::string& json_data, const std::string& output_file) {
    std::string cmd;
    if (output_file.empty()) {
        cmd = std::string(CURL_PATH) + 
            " -s -X POST \"https://api-inference.huggingface.co/models/" + endpoint + "\"" +
            " -H \"Authorization: Bearer " + HF_TOKEN + "\"" +
            " -H \"Content-Type: application/json\"" +
            " -d '" + json_data + "'" +
            " --max-time 120 2>/dev/null";
        return exec(cmd.c_str(), 120);
    } else {
        cmd = std::string(CURL_PATH) + 
            " -s -X POST \"https://api-inference.huggingface.co/models/" + endpoint + "\"" +
            " -H \"Authorization: Bearer " + HF_TOKEN + "\"" +
            " -H \"Content-Type: application/json\"" +
            " -d '" + json_data + "'" +
            " -o \"" + output_file + "\" 2>/dev/null";
        int ret = system(cmd.c_str());
        return (ret == 0) ? "OK" : "ERROR";
    }
}

static bool wait_for_model(const std::string& endpoint) {
    for (int i = 0; i < 30; i++) {
        std::string cmd = std::string(CURL_PATH) + 
            " -s \"https://api-inference.huggingface.co/status/" + endpoint + "\"" +
            " -H \"Authorization: Bearer " + HF_TOKEN + "\" 2>/dev/null";
        std::string status = exec(cmd.c_str(), 10);
        if (status.find("loaded") != std::string::npos || status.find("ready") != std::string::npos) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return false;
}

// ========== ГЕНЕРАЦИЯ МЕДИА ==========

static std::string generate_image(const std::string& prompt, const std::string& filename) {
    // Пробуем сначала лёгкую модель
    std::vector<std::string> models;
    models.push_back("runwayml/stable-diffusion-v1-5");  // Быстрее загружается
    models.push_back("stabilityai/stable-diffusion-2-1"); // Альтернатива
    models.push_back("stabilityai/stable-diffusion-xl-base-1.0"); // Последняя
    
    std::string json = "{\"inputs\": \"" + prompt + "\"}";
    
    for (size_t m = 0; m < models.size(); m++) {
        std::cout << "[Trying model: " << models[m] << "]\n";
        
        if (!wait_for_model(models[m])) {
            std::cout << "[Model " << models[m] << " timeout, trying next...]\n";
            continue;
        }
        
        std::string result = hf_api_post(models[m], json, filename);
        
        if (result == "OK") {
            std::ifstream f(filename.c_str(), std::ios::binary | std::ios::ate);
            if (f && f.tellg() > 1000) {
                return "IMAGE CREATED: " + filename + "\nModel: " + models[m] + "\nPrompt: " + prompt;
            }
        }
    }
    
    return "ERROR: All models failed to load. Try again later or check your HF token.";
}




//GENARATSIYA AUDIO




static std::string generate_audio(const std::string& prompt, const std::string& filename) {
    std::string endpoint = "facebook/musicgen-small";
    std::string json = "{\"inputs\": \"" + prompt + "\"}";
    
    if (!wait_for_model(endpoint)) {
        return "ERROR: Model loading timeout";
    }
    
    std::string result = hf_api_post(endpoint, json, filename);
    
    if (result == "OK") {
        std::ifstream f(filename.c_str(), std::ios::binary | std::ios::ate);
        if (f && f.tellg() > 1000) {
            return "AUDIO CREATED: " + filename + "\nPrompt: " + prompt;
        }
    }
    
    return "ERROR: Failed to create audio";
}

static std::string generate_code(const std::string& prompt) {
    std::string endpoint = "bigcode/starcoder2-3b";
    std::string json = "{\"inputs\": \"// " + prompt + "\\n\", \"parameters\": {\"max_new_tokens\": 512}}";
    
    std::string result = hf_api_post(endpoint, json, "");
    
    if (result.empty() || result.find("error") != std::string::npos) {
        return "ERROR: Code generation failed";
    }
    
    size_t start = result.find("\"generated_text\":\"");
    if (start != std::string::npos) {
        start += 18;
        size_t end = result.rfind("\"");
        if (end != std::string::npos && end > start) {
            result = result.substr(start, end - start);
        }
    }
    
    size_t pos = 0;
    while ((pos = result.find("\\n", pos)) != std::string::npos) {
        result.replace(pos, 2, "\n");
    }
    pos = 0;
    while ((pos = result.find("\\t", pos)) != std::string::npos) {
        result.replace(pos, 2, "\t");
    }
    pos = 0;
    while ((pos = result.find("\\\"", pos)) != std::string::npos) {
        result.replace(pos, 2, "\"");
    }
    
    return "GENERATED CODE:\n```\n" + result + "\n```";
}

static std::string generate_video(const std::string& prompt, const std::string& filename) {
    std::string img_file = filename + "_frame.png";
    std::string result = generate_image(prompt + ", cinematic scene", img_file);
    
    if (result.find("CREATED") != std::string::npos) {
        return "VIDEO (keyframe): " + img_file + 
               "\nNote: Full video requires Runway ML or Pika Labs API separately." +
               "\nPrompt: " + prompt;
    }
    
    return "ERROR: Failed to create video content";
}

// ========== ПУБЛИЧНАЯ ФУНКЦИЯ ДЛЯ ONLINE КОМАНД ==========

std::string process_online_command(const std::string& type, const std::string& prompt) {
    time_t now = time(NULL);
    std::stringstream ss;
    ss << "generated_" << type << "_" << now;
    std::string filename = ss.str();
    
    if (type == "images" || type == "image") {
        filename += ".png";
        return generate_image(prompt, filename);
    } else if (type == "audios" || type == "audio") {
        filename += ".wav";
        return generate_audio(prompt, filename);
    } else if (type == "code") {
        return generate_code(prompt);
    } else if (type == "videos" || type == "video") {
        filename += ".mp4";
        return generate_video(prompt, filename);
    }
    
    return "ERROR: Unknown type: " + type;
}

// ========== ПОИСК ==========

static std::string search_duckduckgo(const std::string& query) {
    static time_t last_search_time = 0;
    time_t now = time(NULL);
    if (difftime(now, last_search_time) < 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    last_search_time = time(NULL);
    
    std::string enc = url_encode(query);
    std::string url = "https://html.duckduckgo.com/html/?q=" + enc;
    
    std::string cmd = std::string(CURL_PATH) + 
        " -s -L --max-time 25" +
        " -H \"User-Agent: Mozilla/5.0\"" +
        " \"" + url + "\" 2>/dev/null";
    
    return exec(cmd.c_str(), 25);
}

static std::string search_bing(const std::string& query) {
    std::string enc = url_encode(query);
    std::string url = "https://www.bing.com/search?q=" + enc;
    
    std::string cmd = std::string(CURL_PATH) + 
        " -s -L --max-time 25" +
        " -H \"User-Agent: Mozilla/5.0\"" +
        " \"" + url + "\" 2>/dev/null";
    
    return exec(cmd.c_str(), 25);
}

struct SearchResult {
    std::string title;
    std::string snippet;
    std::string url;
    int score;
};

static std::vector<SearchResult> extract_search_results(const std::string& html) {
    std::vector<SearchResult> results;
    
    if (html.find("CAPTCHA") != std::string::npos || html.find("captcha") != std::string::npos) {
        return results;
    }
    
    size_t pos = 0;
    while ((pos = html.find("result__a", pos)) != std::string::npos && results.size() < 5) {
        SearchResult res;
        res.score = 0;
        
        size_t href_start = html.find("href=\"", pos);
        if (href_start == std::string::npos) break;
        href_start += 6;
        size_t href_end = html.find("\"", href_start);
        if (href_end == std::string::npos) break;
        res.url = html.substr(href_start, href_end - href_start);
        
        size_t title_start = html.find(">", href_end) + 1;
        size_t title_end = html.find("</a>", title_start);
        if (title_end == std::string::npos) break;
        res.title = strip_html(html.substr(title_start, title_end - title_start));
        
        size_t snippet_start = html.find("result__snippet", title_end);
        if (snippet_start != std::string::npos) {
            snippet_start = html.find(">", snippet_start) + 1;
            size_t snippet_end = html.find("</a>", snippet_start);
            if (snippet_end != std::string::npos) {
                res.snippet = strip_html(html.substr(snippet_start, snippet_end - snippet_start));
            }
        }
        
        size_t uddg_pos = res.url.find("uddg=");
        if (uddg_pos != std::string::npos) {
            std::string encoded = res.url.substr(uddg_pos + 5);
            std::string decoded;
            for (size_t j = 0; j < encoded.length(); j++) {
                if (encoded[j] == '%' && j + 2 < encoded.length()) {
                    int val = 0;
                    sscanf(encoded.substr(j + 1, 2).c_str(), "%x", &val);
                    decoded += (char)val;
                    j += 2;
                } else if (encoded[j] == '&') {
                    break;
                } else {
                    decoded += encoded[j];
                }
            }
            res.url = decoded;
        }
        
        res.score = score_content(res.title) + score_content(res.snippet) * 2;
        
        if (!res.title.empty() && !res.url.empty()) {
            results.push_back(res);
        }
        pos = title_end;
    }
    
    for (size_t i = 0; i < results.size(); i++) {
        for (size_t j = i + 1; j < results.size(); j++) {
            if (results[j].score > results[i].score) {
                SearchResult tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }
        }
    }
    
    return results;
}

static std::string fetch_best_content(const std::string& query, int min_chars, int max_chars) {
    std::string html = search_duckduckgo(query);
    std::vector<SearchResult> results = extract_search_results(html);
    
    if (results.empty()) {
        html = search_bing(query);
        results = extract_search_results(html);
        if (results.empty()) {
            return "";
        }
    }
    
    SearchResult best = results[0];
    std::string page_content = http_get(best.url);
    if (page_content.empty() || (int)page_content.length() < 500) {
        std::string fallback = best.title + "\n\n" + best.snippet;
        for (size_t i = 1; i < results.size() && (int)fallback.length() < min_chars; i++) {
            fallback += "\n\n" + results[i].title + "\n" + results[i].snippet;
        }
        return fallback;
    }
    
    std::string text = strip_html(page_content);
    
    size_t start = 0;
    std::vector<std::string> markers;
    markers.push_back("Main content");
    markers.push_back("Article");
    markers.push_back("Content");
    
    for (size_t m = 0; m < markers.size(); m++) {
        size_t pos = text.find(markers[m]);
        if (pos != std::string::npos && pos < text.length() / 3) {
            start = pos + markers[m].length();
            break;
        }
    }
    if (start > 0 && start < text.length()) {
        text = text.substr(start);
    }
    
    size_t end = text.length();
    std::vector<std::string> end_markers;
    end_markers.push_back("Comments");
    end_markers.push_back("Related");
    end_markers.push_back("References");
    
    for (size_t m = 0; m < end_markers.size(); m++) {
        size_t pos = text.find(end_markers[m], text.length() / 2);
        if (pos != std::string::npos) {
            if (pos < end) end = pos;
        }
    }
    if (end < text.length()) {
        text = text.substr(0, end);
    }
    
    if ((int)text.length() > max_chars) {
        text = text.substr(0, max_chars);
        size_t last_dot = text.rfind('.');
        if (last_dot != std::string::npos && last_dot > (size_t)min_chars) {
            text = text.substr(0, last_dot + 1);
        }
    }
    
    if ((int)text.length() < min_chars && results.size() > 1) {
        text += "\n\nAdditional:\n";
        for (size_t i = 1; i < results.size() && (int)text.length() < min_chars; i++) {
            text += "\n" + results[i].title + ": " + results[i].snippet;
        }
    }
    
    return text;
}

std::string cmd_search(const std::string& query) {
    std::string content = fetch_best_content(query, 2048, 8192);
    if (content.empty()) {
        return "ERROR: No results for: " + query;
    }
    return "INFO [" + query + "]:\n\n" + content;
}

std::string get_search_context(const std::string& query, int max_chars) {
    std::string search_results = cmd_search(query);
    if (search_results.find("ERROR:") == 0) {
        return "Answer using your knowledge: " + query;
    }
    if ((int)search_results.length() > max_chars) {
        search_results = search_results.substr(0, max_chars);
        size_t last_dot = search_results.rfind('.');
        if (last_dot != std::string::npos && last_dot > (size_t)max_chars * 0.8) {
            search_results = search_results.substr(0, last_dot + 1);
        }
    }
    return "Based on this info, answer.\n\n" +
           search_results + "\n\nQuestion: " + query + "\n\nAnswer:";
}

// ========== AGENT И ДРУГИЕ ФУНКЦИИ ==========

static std::string to_lower(std::string s) {
    for (size_t i = 0; i < s.length(); i++) {
        s[i] = tolower((unsigned char)s[i]);
    }
    return s;
}

static bool starts_with(const std::string& str, const std::string& prefix) {
    if (str.length() < prefix.length()) return false;
    return str.substr(0, prefix.length()) == prefix;
}

static std::string trim_end_punctuation(std::string s) {
    while (!s.empty()) {
        char c = s[s.length()-1];
        if (c == '?' || c == '.' || c == '!' || c == ',' || c == ';' || c == ':') {
            s = s.substr(0, s.length()-1);
        } else {
            break;
        }
    }
    return s;
}

std::string cmd_agent(const std::string& task) {
    std::string lt = to_lower(task);
    
    if (starts_with(lt, "/search ") || starts_with(lt, "search ") || 
        starts_with(lt, "найди ") || lt.find("что такое ") != std::string::npos ||
        lt.find("кто такой ") != std::string::npos) {
        std::string q = task;
        std::vector<std::string> prefixes;
        prefixes.push_back("/search ");
        prefixes.push_back("search ");
        prefixes.push_back("найди ");
        prefixes.push_back("что такое ");
        prefixes.push_back("кто такой ");
        for (size_t i = 0; i < prefixes.size(); i++) {
            size_t pos = q.find(prefixes[i]);
            if (pos != std::string::npos) {
                q = q.substr(pos + prefixes[i].length());
                break;
            }
        }
        q = trim_end_punctuation(q);
        if (!q.empty()) {
            return get_search_context(q, 6000);
        }
    }
    
    if (starts_with(lt, "fetch ") || starts_with(lt, "get url ") || 
        starts_with(lt, "собери ") || task.find("http") != std::string::npos) {
        size_t http_pos = task.find("http");
        if (http_pos != std::string::npos) {
            size_t space_after = task.find(" ", http_pos);
            std::string url;
            if (space_after == std::string::npos) {
                url = task.substr(http_pos);
            } else {
                url = task.substr(http_pos, space_after - http_pos);
            }
            
            size_t uddg_pos = url.find("uddg=");
            if (uddg_pos != std::string::npos) {
                std::string encoded = url.substr(uddg_pos + 5);
                std::string decoded;
                for (size_t i = 0; i < encoded.length(); i++) {
                    if (encoded[i] == '%' && i + 2 < encoded.length()) {
                        int val = 0;
                        sscanf(encoded.substr(i + 1, 2).c_str(), "%x", &val);
                        decoded += (char)val;
                        i += 2;
                    } else if (encoded[i] == '&') { 
                        break; 
                    } else { 
                        decoded += encoded[i]; 
                    }
                }
                url = decoded;
            }
            
            std::string content = http_get(url);
            if (content.empty()) {
                return "ERROR: Failed to load " + url;
            }
            
            std::string text = strip_html(content);
            if ((int)text.length() > 8000) {
                text = text.substr(0, 8000);
                size_t last_dot = text.rfind('.');
                if (last_dot != std::string::npos && last_dot > 6000) {
                    text = text.substr(0, last_dot + 1);
                }
                text += "\n\n[Truncated...]";
            }
            
            return "CONTENT [" + url + "]:\n\n" + text;
        }
    }
    
    return task;
}






#include <string>
#include <ctime>

std::string cmd_date() {
    time_t now = time(nullptr);
    tm timeinfo;

    localtime_r(&now, &timeinfo);  // безопасная версия

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);

    return std::string(buffer);
}

std::string cmd_time(int64_t ts) {
    time_t raw_time = (ts > 0) ? (time_t)ts : time(nullptr);
    tm timeinfo;

    localtime_r(&raw_time, &timeinfo);  // безопасная версия

    char buffer[64];
    strftime(buffer, sizeof(buffer), " %H:%M:%S", &timeinfo);

    return std::string(buffer);
}










std::string shell_exec(const std::string& c) { 
    return exec(c.c_str(), 30); 
}

std::string file_read(const std::string& p) {
    std::ifstream f(p.c_str());
    if (!f) return "ERROR: Cannot read file " + p;
    std::stringstream b;
    b << f.rdbuf();
    return b.str();
}

bool file_write(const std::string& path, const std::string& content) {
    std::ofstream f(path.c_str());
    if (!f) return false;
    f << content;
    return f.good();
}

} // namespace llama_tools
