#include "crawler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

std::string zeroPad(int value, int width) {
    std::ostringstream padded;
    padded << std::setw(width) << std::setfill('0') << value;
    return padded.str();
}

int digitsFor(int count) {
    int width = 1;
    while (count >= 10) {
        count /= 10;
        ++width;
    }
    return std::max(width, 3);
}

}

Crawler::Crawler(const CrawlerConfig& config)
    : config_(config),
      limiter_(std::chrono::milliseconds(config.delayMs)),
      cursor_(0),
      downloaded_(0),
      skipped_(0),
      failed_(0),
      aborted_(false) {
    httpOptions_.userAgent = config_.userAgent;
    httpOptions_.cookies = config_.cookies;
    httpOptions_.proxy = config_.proxy;
    httpOptions_.retries = config_.retries;
}

void Crawler::report(const std::string& message) {
    if (config_.quiet) {
        return;
    }
    std::lock_guard<std::mutex> guard(consoleMutex_);
    std::cout << message << std::endl;
}

void Crawler::reportError(const std::string& message) {
    std::lock_guard<std::mutex> guard(consoleMutex_);
    std::cerr << message << std::endl;
}

bool Crawler::fetchGallery() {
    std::string host;
    if (!ehentai::parseGalleryUrl(config_.url, host, gallery_.gid, gallery_.token)) {
        reportError("not a gallery url: " + config_.url);
        reportError("expected something like https://" EH_HOST "/g/<id>/<token>/");
        return false;
    }
    gallery_.host = host;
    gallery_.url = ehentai::normaliseGalleryUrl(config_.url);

    http::Client client(httpOptions_, &limiter_);
    int listingPages = 1;
    for (int listing = 0; listing < listingPages; ++listing) {
        std::string url = ehentai::listingUrl(gallery_.url, listing);
        http::Response response;
        if (!client.get(url, response, gallery_.url)) {
            reportError("couldn't fetch " + url + ": " + client.lastError());
            if (client.lastStatus() == 404 && host == EX_HOST) {
                reportError("exhentai needs a logged in session, pass --cookies");
            }
            return false;
        }
        std::string reason;
        if (ehentai::isBlockedResponse(response.body, reason)) {
            reportError("the site said: " + reason);
            return false;
        }
        if (!ehentai::parseGallery(response.body, gallery_)) {
            reportError("couldn't parse the gallery page at " + url);
            return false;
        }
        if (listing == 0) {
            listingPages = gallery_.listingPages();
            if (gallery_.title().empty()) {
                reportError("no gallery title found, is " + url + " really a gallery?");
                return false;
            }
            report("gallery: " + gallery_.title());
            if (!gallery_.titleJapanese.empty() && gallery_.titleJapanese != gallery_.title()) {
                report("         " + gallery_.titleJapanese);
            }
            report("images:  " + std::to_string(gallery_.imageCount) + " across " +
                   std::to_string(listingPages) + " listing page(s)");
        }
    }

    std::sort(gallery_.pages.begin(), gallery_.pages.end(),
              [](const ehentai::PageRef& a, const ehentai::PageRef& b) { return a.index < b.index; });
    gallery_.pages.erase(std::unique(gallery_.pages.begin(), gallery_.pages.end(),
                                     [](const ehentai::PageRef& a, const ehentai::PageRef& b) {
                                         return a.index == b.index;
                                     }),
                         gallery_.pages.end());

    if (gallery_.pages.empty()) {
        reportError("the gallery listing had no image links");
        return false;
    }
    if (gallery_.imageCount == 0) {
        gallery_.imageCount = static_cast<int>(gallery_.pages.size());
    }
    if (static_cast<int>(gallery_.pages.size()) != gallery_.imageCount) {
        report("warning: found " + std::to_string(gallery_.pages.size()) + " of " +
               std::to_string(gallery_.imageCount) + " image links");
    }
    return true;
}

bool Crawler::prepareOutputDir() {
    outputPath_ = std::filesystem::path(config_.outputDir);
    if (!config_.flat) {
        outputPath_ /= ehentai::sanitiseFileName(gallery_.title());
    }
    std::error_code ec;
    std::filesystem::create_directories(outputPath_, ec);
    if (ec && !std::filesystem::is_directory(outputPath_)) {
        reportError("couldn't create " + ehentai::pathToUtf8(outputPath_) + ": " + ec.message());
        return false;
    }
    report("output:  " + ehentai::pathToUtf8(outputPath_));
    return true;
}

void Crawler::writeMetadata() {
    std::filesystem::path target = outputPath_ / "metadata.json";
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        reportError("couldn't write " + ehentai::pathToUtf8(target));
        return;
    }
    out << ehentai::toJson(gallery_);
}

std::filesystem::path Crawler::pathFor(const ehentai::PageRef& page, const ehentai::ImagePage& image,
                                       const std::string& source) const {
    std::string name = image.fileName.empty() ? page.fileName : image.fileName;
    std::string stem = name;
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) {
        stem.erase(dot);
    }
    std::string extension = ehentai::extensionOf(source, name.empty() ? ".jpg" : ehentai::extensionOf(name));

    std::string leaf = zeroPad(page.index, digitsFor(gallery_.imageCount));
    if (!stem.empty()) {
        leaf += "_" + stem;
    }
    return outputPath_ / (ehentai::sanitiseFileName(leaf) + extension);
}

bool Crawler::alreadyOnDisk(const std::filesystem::path& target) const {
    if (config_.overwrite) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
        return false;
    }
    std::uintmax_t size = std::filesystem::file_size(target, ec);
    return !ec && size > 0;
}

// Downloads one image and makes sure the bytes really are an image. The site
// answers with an HTML login or throttling page on a 200, which would otherwise
// land on disk wearing a .png suffix.
bool Crawler::saveImage(http::Client& client, const std::string& source,
                        const std::filesystem::path& target, const std::string& referer,
                        std::string& error) {
    if (source.empty()) {
        error = "no image url on the page";
        return false;
    }
    if (source.find(QUOTA_EXCEEDED_MARKER) != std::string::npos) {
        error = "image quota exhausted";
        aborted_.store(true);
        return false;
    }
    if (!client.download(source, target, referer)) {
        error = client.lastError();
        if (client.lastStatus() == 509) {
            error = "image quota exhausted (HTTP 509)";
            aborted_.store(true);
        }
        return false;
    }
    const std::string& contentType = client.lastContentType();
    if (!contentType.empty() && contentType.rfind("image/", 0) != 0) {
        std::error_code ec;
        std::filesystem::remove(target, ec);
        error = "server sent " + contentType + " instead of an image";
        return false;
    }
    return true;
}

bool Crawler::fetchOne(http::Client& client, const ehentai::PageRef& page) {
    std::string label = "page " + std::to_string(page.index);
    ehentai::ImagePage image;
    if (!loadImagePage(client, page.url, page.index, image)) {
        return false;
    }

    std::string error;
    bool wantOriginal = config_.original && !image.originalUrl.empty();
    if (config_.original && image.originalUrl.empty()) {
        report(label + ": no original offered, taking the resample");
    }

    if (wantOriginal) {
        std::filesystem::path target = pathFor(page, image, image.originalUrl);
        if (alreadyOnDisk(target)) {
            skipped_.fetch_add(1);
            report("skip  " + ehentai::pathToUtf8(target.filename()));
            return true;
        }
        if (saveImage(client, image.originalUrl, target, page.url, error)) {
            downloaded_.fetch_add(1);
            report("save  " + ehentai::pathToUtf8(target.filename()));
            return true;
        }
        if (aborted_.load()) {
            reportError(label + ": " + error);
            return false;
        }
        report(label + ": original unavailable (" + error + "), taking the resample");
    }

    std::filesystem::path target = pathFor(page, image, image.imageUrl);
    if (alreadyOnDisk(target)) {
        skipped_.fetch_add(1);
        report("skip  " + ehentai::pathToUtf8(target.filename()));
        return true;
    }
    if (saveImage(client, image.imageUrl, target, page.url, error)) {
        downloaded_.fetch_add(1);
        report("save  " + ehentai::pathToUtf8(target.filename()));
        return true;
    }
    if (aborted_.load()) {
        reportError(label + ": " + error);
        return false;
    }

    // The Hath node handing out this image can be dead; the reload token asks
    // the site for a different one.
    if (image.reloadToken.empty()) {
        reportError(label + ": " + error);
        return false;
    }
    report(label + ": " + error + ", retrying on another host");
    ehentai::ImagePage reloaded;
    if (!loadImagePage(client, ehentai::reloadUrl(page.url, image.reloadToken), page.index, reloaded)) {
        return false;
    }
    std::filesystem::path retryTarget = pathFor(page, reloaded, reloaded.imageUrl);
    if (saveImage(client, reloaded.imageUrl, retryTarget, page.url, error)) {
        downloaded_.fetch_add(1);
        report("save  " + ehentai::pathToUtf8(retryTarget.filename()));
        return true;
    }
    reportError(label + ": " + error);
    return false;
}

bool Crawler::loadImagePage(http::Client& client, const std::string& url, int index,
                            ehentai::ImagePage& image) {
    std::string label = "page " + std::to_string(index);
    http::Response response;
    if (!client.get(url, response, gallery_.url)) {
        reportError(label + ": " + client.lastError());
        return false;
    }
    std::string reason;
    if (ehentai::isBlockedResponse(response.body, reason)) {
        reportError(label + ": " + reason);
        aborted_.store(true);
        return false;
    }
    image.index = index;
    if (!ehentai::parseImagePage(response.body, image)) {
        reportError(label + ": couldn't find the image on " + url);
        return false;
    }
    return true;
}

void Crawler::worker(size_t) {
    http::Client client(httpOptions_, &limiter_);
    while (!aborted_.load()) {
        size_t index = cursor_.fetch_add(1);
        if (index >= queue_.size()) {
            return;
        }
        if (!fetchOne(client, queue_[index])) {
            failed_.fetch_add(1);
        }
    }
}

void Crawler::downloadAll() {
    int jobs = std::max(1, std::min(config_.jobs, static_cast<int>(queue_.size())));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(jobs));
    for (int i = 0; i < jobs; ++i) {
        workers.emplace_back(&Crawler::worker, this, static_cast<size_t>(i));
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
}

int Crawler::run() {
    if (!fetchGallery() || !prepareOutputDir()) {
        return 1;
    }
    writeMetadata();
    if (config_.metadataOnly) {
        report("wrote metadata.json, stopping as asked");
        return 0;
    }

    int last = config_.lastPage > 0 ? config_.lastPage : gallery_.imageCount;
    for (const ehentai::PageRef& page : gallery_.pages) {
        if (page.index >= config_.firstPage && page.index <= last) {
            queue_.push_back(page);
        }
    }
    if (queue_.empty()) {
        reportError("no pages left after applying --from/--to");
        return 1;
    }
    report("fetching " + std::to_string(queue_.size()) + " image(s) with " +
           std::to_string(std::max(1, config_.jobs)) + " worker(s)");

    downloadAll();

    report("done: " + std::to_string(downloaded_.load()) + " downloaded, " +
           std::to_string(skipped_.load()) + " already present, " + std::to_string(failed_.load()) +
           " failed");
    if (aborted_.load()) {
        reportError("stopped early, the site cut the crawl short");
        return 2;
    }
    return failed_.load() == 0 ? 0 : 1;
}
