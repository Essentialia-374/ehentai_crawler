#pragma once

#include <curl/curl.h>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>

#include "macros.h"

namespace http {

struct Options {
    std::string userAgent = USER_AGENT;
    std::string cookies;
    std::string proxy;
    long timeoutSeconds = DEFAULT_TIMEOUT_SECONDS;
    long connectTimeoutSeconds = DEFAULT_CONNECT_TIMEOUT_SECONDS;
    int retries = DEFAULT_RETRIES;
};

struct Response {
    long status = 0;
    std::string body;
    std::string contentType;
    std::string effectiveUrl;

    bool ok() const { return status >= 200 && status < 300; }
};

// Keeps every worker at least `interval` apart so the whole crawl stays within
// what the site tolerates, no matter how many threads are running.
class RateLimiter {
public:
    explicit RateLimiter(std::chrono::milliseconds interval);

    void wait();

private:
    std::chrono::milliseconds interval_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point next_;
};

// Wraps a single curl easy handle, so give every thread its own instance.
class Client {
public:
    Client(const Options& options, RateLimiter* limiter);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool get(const std::string& url, Response& response, const std::string& referer = std::string());
    bool download(const std::string& url, const std::filesystem::path& path, const std::string& referer = std::string());

    const std::string& lastError() const { return lastError_; }
    const std::string& lastContentType() const { return lastContentType_; }
    long lastStatus() const { return lastStatus_; }

private:
    void applyCommonOptions(const std::string& url, const std::string& referer);
    bool perform(const std::string& url, long& status, bool& retryable);

    CURL* curl_;
    Options options_;
    RateLimiter* limiter_;
    std::string lastError_;
    std::string lastContentType_;
    long lastStatus_;
};

bool globalInit();
void globalCleanup();

}
