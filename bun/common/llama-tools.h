#pragma once
#include <string>
#include <cstdint>

namespace llama_tools {
    std::string cmd_search(const std::string& query);
    std::string get_search_context(const std::string& query, int max_chars);
    std::string cmd_date();
    std::string cmd_time(int64_t ts);
    std::string cmd_agent(const std::string& task);
    std::string http_get(const std::string& url);
    std::string shell_exec(const std::string& cmd);
    std::string file_read(const std::string& path);
    bool file_write(const std::string& path, const std::string& content);
    std::string process_online_command(const std::string& type, const std::string& prompt);
}
