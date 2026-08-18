#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "ehentai.h"
#include "http.h"
#include "macros.h"

struct CrawlerConfig {
    std::string url;
    std::string outputDir = DEFAULT_OUTPUT_DIR;
    std::string cookies;
    std::string proxy;
    std::string userAgent = USER_AGENT;
    int jobs = DEFAULT_JOBS;
    int delayMs = DEFAULT_DELAY_MS;
    int retries = DEFAULT_RETRIES;
    int firstPage = 1;
    int lastPage = 0;
    bool original = false;
    bool metadataOnly = false;
    bool overwrite = false;
    bool flat = false;
    bool quiet = false;
};

class Crawler {
public:
    explicit Crawler(const CrawlerConfig& config);

    // Returns the process exit code: 0 when every requested image landed on disk.
    int run();

private:
    bool fetchGallery();
    bool prepareOutputDir();
    void writeMetadata();
    void downloadAll();
    void worker(size_t workerIndex);
    bool fetchOne(http::Client& client, const ehentai::PageRef& page);
    bool loadImagePage(http::Client& client, const std::string& url, int index, ehentai::ImagePage& image);
    bool saveImage(http::Client& client, const std::string& source, const std::filesystem::path& target,
                   const std::string& referer, std::string& error);
    bool alreadyOnDisk(const std::filesystem::path& target) const;
    std::filesystem::path pathFor(const ehentai::PageRef& page, const ehentai::ImagePage& image,
                                  const std::string& source) const;
    void report(const std::string& message);
    void reportError(const std::string& message);

    CrawlerConfig config_;
    http::Options httpOptions_;
    http::RateLimiter limiter_;
    ehentai::Gallery gallery_;
    std::vector<ehentai::PageRef> queue_;
    std::filesystem::path outputPath_;

    std::mutex consoleMutex_;
    std::atomic<size_t> cursor_;
    std::atomic<int> downloaded_;
    std::atomic<int> skipped_;
    std::atomic<int> failed_;
    std::atomic<bool> aborted_;
};
