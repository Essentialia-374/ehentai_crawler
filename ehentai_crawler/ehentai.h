#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ehentai {

struct Tag {
    std::string ns;
    std::string name;
};

// One entry of the gallery thumbnail grid, i.e. one image viewer page.
struct PageRef {
    int index = 0;
    std::string url;
    std::string fileName;
};

struct Gallery {
    std::string url;
    std::string host;
    std::string gid;
    std::string token;
    std::string titleEnglish;
    std::string titleJapanese;
    std::string rating;
    std::map<std::string, std::string> details;
    std::vector<Tag> tags;
    int imageCount = 0;
    int imagesPerPage = 0;
    std::vector<PageRef> pages;

    const std::string& title() const;
    int listingPages() const;
};

struct ImagePage {
    int index = 0;
    std::string imageUrl;
    std::string originalUrl;
    std::string fileName;
    std::string reloadToken;
    std::string nextUrl;
};

bool parseGalleryUrl(const std::string& url, std::string& host, std::string& gid, std::string& token);
std::string normaliseGalleryUrl(const std::string& url);
std::string listingUrl(const std::string& galleryUrl, int listingPage);
std::string reloadUrl(const std::string& imagePageUrl, const std::string& reloadToken);

// Fills the metadata fields and appends the thumbnails found on this listing
// page to gallery.pages. Returns false when the document is not a gallery page.
bool parseGallery(const std::string& body, Gallery& gallery);
bool parseImagePage(const std::string& body, ImagePage& page);

// Reports the site's own "your IP has been banned" / quota interstitials, which
// come back as a normal 200 response.
bool isBlockedResponse(const std::string& body, std::string& reason);

std::string sanitiseFileName(const std::string& name);

// path::string() re-encodes to the active narrow code page on Windows, which
// mangles the CJK titles galleries routinely carry. Everything we print or log
// goes through here instead.
std::string pathToUtf8(const std::filesystem::path& path);
std::string extensionOf(const std::string& url, const std::string& fallback = ".jpg");
std::string toJson(const Gallery& gallery);

}
