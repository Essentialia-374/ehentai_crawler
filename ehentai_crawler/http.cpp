#include "http.h"

#include <cstdio>
#include <thread>

#include "ehentai.h"

namespace http {

namespace {

size_t writeToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, total);
    return total;
}

size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    return std::fwrite(ptr, 1, total, static_cast<std::FILE*>(userdata));
}

// fopen takes narrow paths, which on Windows cannot spell every file name the
// crawler produces; _wfopen takes the native wide path instead.
std::FILE* openForWrite(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), L"wb") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

bool worthRetrying(CURLcode code, long status) {
    if (code != CURLE_OK) {
        return code != CURLE_UNSUPPORTED_PROTOCOL && code != CURLE_URL_MALFORMAT &&
               code != CURLE_TOO_MANY_REDIRECTS;
    }
    // 509 is the site's "bandwidth exceeded" answer; hammering it only digs deeper.
    return status == 0 || status == 408 || status == 429 || (status >= 500 && status != 509);
}

}

RateLimiter::RateLimiter(std::chrono::milliseconds interval)
    : interval_(interval), next_(std::chrono::steady_clock::now()) {
}

void RateLimiter::wait() {
    if (interval_.count() <= 0) {
        return;
    }
    std::chrono::steady_clock::time_point slot;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        slot = next_ > now ? next_ : now;
        next_ = slot + interval_;
    }
    std::this_thread::sleep_until(slot);
}

Client::Client(const Options& options, RateLimiter* limiter)
    : curl_(curl_easy_init()), options_(options), limiter_(limiter), lastStatus_(0) {
    if (!curl_) {
        lastError_ = "couldn't init curl";
    }
}

Client::~Client() {
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
}

void Client::applyCommonOptions(const std::string& url, const std::string& referer) {
    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, options_.userAgent.c_str());
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, options_.timeoutSeconds);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, options_.connectTimeoutSeconds);
    curl_easy_setopt(curl_, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
    if (!options_.cookies.empty()) {
        curl_easy_setopt(curl_, CURLOPT_COOKIE, options_.cookies.c_str());
    }
    if (!options_.proxy.empty()) {
        curl_easy_setopt(curl_, CURLOPT_PROXY, options_.proxy.c_str());
    }
    if (!referer.empty()) {
        curl_easy_setopt(curl_, CURLOPT_REFERER, referer.c_str());
    }
}

bool Client::perform(const std::string& url, long& status, bool& retryable) {
    if (limiter_) {
        limiter_->wait();
    }
    CURLcode code = curl_easy_perform(curl_);
    status = 0;
    lastContentType_.clear();
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status);
    lastStatus_ = status;
    if (code != CURLE_OK) {
        lastError_ = curl_easy_strerror(code);
        retryable = worthRetrying(code, status);
        return false;
    }
    if (status < 200 || status >= 300) {
        lastError_ = "HTTP " + std::to_string(status) + " for " + url;
        retryable = worthRetrying(code, status);
        return false;
    }
    const char* contentType = nullptr;
    curl_easy_getinfo(curl_, CURLINFO_CONTENT_TYPE, &contentType);
    if (contentType) {
        lastContentType_ = contentType;
        size_t parameters = lastContentType_.find(';');
        if (parameters != std::string::npos) {
            lastContentType_.erase(parameters);
        }
    }
    retryable = false;
    lastError_.clear();
    return true;
}

bool Client::get(const std::string& url, Response& response, const std::string& referer) {
    if (!curl_) {
        return false;
    }
    for (int attempt = 0; attempt <= options_.retries; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (1 << (attempt - 1))));
        }
        response.body.clear();
        applyCommonOptions(url, referer);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeToString);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);

        long status = 0;
        bool retryable = false;
        bool succeeded = perform(url, status, retryable);
        response.status = status;
        if (succeeded) {
            const char* effectiveUrl = nullptr;
            curl_easy_getinfo(curl_, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
            response.contentType = lastContentType_;
            response.effectiveUrl = effectiveUrl ? effectiveUrl : url;
            return true;
        }
        if (!retryable) {
            return false;
        }
    }
    return false;
}

bool Client::download(const std::string& url, const std::filesystem::path& path, const std::string& referer) {
    if (!curl_) {
        return false;
    }
    const std::filesystem::path& target = path;
    std::filesystem::path partial = target;
    partial += ".part";

    for (int attempt = 0; attempt <= options_.retries; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (1 << (attempt - 1))));
        }
        std::FILE* file = openForWrite(partial);
        if (!file) {
            lastError_ = "couldn't open " + ehentai::pathToUtf8(partial);
            return false;
        }
        applyCommonOptions(url, referer);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeToFile);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, file);

        long status = 0;
        bool retryable = false;
        bool succeeded = perform(url, status, retryable);
        std::fclose(file);

        if (succeeded) {
            std::error_code ec;
            std::filesystem::rename(partial, target, ec);
            if (ec) {
                lastError_ = "couldn't rename " + ehentai::pathToUtf8(partial) + ": " + ec.message();
                std::filesystem::remove(partial, ec);
                return false;
            }
            return true;
        }

        std::error_code ec;
        std::filesystem::remove(partial, ec);
        if (!retryable) {
            return false;
        }
    }
    return false;
}

bool globalInit() {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void globalCleanup() {
    curl_global_cleanup();
}

}
