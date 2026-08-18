#include "ehentai.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <regex>
#include <sstream>

#include "html.h"
#include "macros.h"

namespace ehentai {

namespace {

const std::regex kGalleryUrl(R"(^https?://([^/]+)/g/(\d+)/([0-9a-f]+))", std::regex::icase);
const std::regex kImagePageUrl(R"(/s/[0-9a-f]+/(\d+)-(\d+))", std::regex::icase);
const std::regex kShowingCount(R"(Showing\s+\d+\s*-\s*(\d+)\s+of\s+([\d,]+))", std::regex::icase);
const std::regex kThumbTitle(R"(^Page\s+(\d+):\s*(.+)$)", std::regex::icase);
const std::regex kReloadCall(R"(nl\(\s*'([^']+)'\s*\))");

int toInt(const std::string& value) {
    std::string digits;
    for (char c : value) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
        }
    }
    if (digits.empty()) {
        return 0;
    }
    try {
        return std::stoi(digits);
    } catch (const std::exception&) {
        return 0;
    }
}

std::string trim(const std::string& value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return std::string();
    }
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string idText(const GumboNode* root, const char* id) {
    const GumboNode* node = html::elementById(root, id);
    return node ? html::text(node) : std::string();
}

// "1_01a.png :: 1280 x 960 :: 133.0 KiB" -> "1_01a.png"
std::string fileNameFromCaption(const std::string& caption) {
    size_t separator = caption.find(" :: ");
    return trim(separator == std::string::npos ? caption : caption.substr(0, separator));
}

void parseDetails(const GumboNode* root, Gallery& gallery) {
    const GumboNode* gdd = html::elementById(root, "gdd");
    if (!gdd) {
        return;
    }
    for (const GumboNode* row : html::allByTag(gdd, GUMBO_TAG_TR)) {
        std::string key;
        std::string value;
        for (const GumboNode* cell : html::childElements(row)) {
            if (html::hasClass(cell, "gdt1")) {
                key = html::text(cell);
            } else if (html::hasClass(cell, "gdt2")) {
                value = html::text(cell);
            }
        }
        if (!key.empty()) {
            if (!key.empty() && key.back() == ':') {
                key.pop_back();
            }
            gallery.details[key] = value;
        }
    }
}

void parseTags(const GumboNode* root, Gallery& gallery) {
    const GumboNode* taglist = html::elementById(root, "taglist");
    if (!taglist) {
        return;
    }
    for (const GumboNode* row : html::allByTag(taglist, GUMBO_TAG_TR)) {
        std::string ns;
        for (const GumboNode* cell : html::childElements(row)) {
            if (html::hasClass(cell, "tc")) {
                ns = html::text(cell);
                if (!ns.empty() && ns.back() == ':') {
                    ns.pop_back();
                }
                continue;
            }
            for (const GumboNode* link : html::allByTag(cell, GUMBO_TAG_A)) {
                std::string name = html::text(link);
                if (!name.empty()) {
                    gallery.tags.push_back(Tag{ns, name});
                }
            }
        }
    }
}

void parseThumbnails(const GumboNode* root, Gallery& gallery) {
    const GumboNode* gdt = html::elementById(root, "gdt");
    if (!gdt) {
        return;
    }
    for (const GumboNode* link : html::allByTag(gdt, GUMBO_TAG_A)) {
        std::string href = html::attributeOr(link, "href");
        std::smatch match;
        if (!std::regex_search(href, match, kImagePageUrl)) {
            continue;
        }
        PageRef page;
        page.index = toInt(match[2].str());
        page.url = href;

        const GumboNode* titled = html::findFirst(link, [](const GumboNode* node) {
            return html::attribute(node, "title") != nullptr;
        });
        if (titled) {
            std::string title = html::attributeOr(titled, "title");
            std::smatch titleMatch;
            if (std::regex_match(title, titleMatch, kThumbTitle)) {
                page.fileName = trim(titleMatch[2].str());
            }
        }
        gallery.pages.push_back(page);
    }
}

void parseCounts(const GumboNode* root, Gallery& gallery) {
    for (const GumboNode* node : html::allByClass(root, "gpc")) {
        std::string caption = html::text(node);
        std::smatch match;
        if (std::regex_search(caption, match, kShowingCount)) {
            gallery.imagesPerPage = std::max(gallery.imagesPerPage, toInt(match[1].str()));
            gallery.imageCount = std::max(gallery.imageCount, toInt(match[2].str()));
            break;
        }
    }
    if (gallery.imageCount == 0) {
        std::map<std::string, std::string>::const_iterator length = gallery.details.find("Length");
        if (length != gallery.details.end()) {
            gallery.imageCount = toInt(length->second);
        }
    }
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char raw : value) {
        unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                escaped += buffer;
            } else {
                escaped.push_back(raw);
            }
        }
    }
    return escaped;
}

}

const std::string& Gallery::title() const {
    return titleEnglish.empty() ? titleJapanese : titleEnglish;
}

int Gallery::listingPages() const {
    if (imageCount <= 0 || imagesPerPage <= 0) {
        return 1;
    }
    return (imageCount + imagesPerPage - 1) / imagesPerPage;
}

bool parseGalleryUrl(const std::string& url, std::string& host, std::string& gid, std::string& token) {
    std::smatch match;
    if (!std::regex_search(url, match, kGalleryUrl)) {
        return false;
    }
    host = match[1].str();
    gid = match[2].str();
    token = match[3].str();
    return true;
}

std::string normaliseGalleryUrl(const std::string& url) {
    std::string host;
    std::string gid;
    std::string token;
    if (!parseGalleryUrl(url, host, gid, token)) {
        return url;
    }
    return "https://" + host + "/g/" + gid + "/" + token + "/";
}

std::string listingUrl(const std::string& galleryUrl, int listingPage) {
    std::string base = normaliseGalleryUrl(galleryUrl);
    if (listingPage <= 0) {
        return base;
    }
    return base + "?p=" + std::to_string(listingPage);
}

std::string reloadUrl(const std::string& imagePageUrl, const std::string& reloadToken) {
    if (reloadToken.empty()) {
        return imagePageUrl;
    }
    std::string base = imagePageUrl;
    size_t query = base.find('?');
    if (query != std::string::npos) {
        base.erase(query);
    }
    return base + "?nl=" + reloadToken;
}

bool parseGallery(const std::string& body, Gallery& gallery) {
    html::Document document(body);
    if (!document.valid()) {
        return false;
    }
    const GumboNode* root = document.root();

    std::string english = idText(root, "gn");
    std::string japanese = idText(root, "gj");
    if (!english.empty()) {
        gallery.titleEnglish = english;
    }
    if (!japanese.empty()) {
        gallery.titleJapanese = japanese;
    }

    if (gallery.rating.empty()) {
        std::string label = idText(root, "rating_label");
        size_t colon = label.find(':');
        gallery.rating = colon == std::string::npos ? label : trim(label.substr(colon + 1));
    }

    if (gallery.details.empty()) {
        parseDetails(root, gallery);
    }
    if (gallery.tags.empty()) {
        parseTags(root, gallery);
    }
    parseCounts(root, gallery);

    size_t before = gallery.pages.size();
    parseThumbnails(root, gallery);
    return gallery.pages.size() > before || !gallery.title().empty();
}

bool parseImagePage(const std::string& body, ImagePage& page) {
    html::Document document(body);
    if (!document.valid()) {
        return false;
    }
    const GumboNode* root = document.root();

    const GumboNode* image = html::elementById(root, "img");
    if (!image) {
        const GumboNode* container = html::elementById(root, "i3");
        image = container ? html::firstByTag(container, GUMBO_TAG_IMG) : nullptr;
    }
    if (!image) {
        return false;
    }
    page.imageUrl = html::attributeOr(image, "src");
    if (page.imageUrl.empty()) {
        return false;
    }

    // The caption under the viewer carries the name the uploader used.
    for (const char* id : {"i2", "i4"}) {
        const GumboNode* block = html::elementById(root, id);
        if (!block) {
            continue;
        }
        for (const GumboNode* child : html::childElements(block)) {
            std::string caption = html::text(child);
            if (caption.find(" :: ") != std::string::npos) {
                page.fileName = fileNameFromCaption(caption);
                break;
            }
        }
        if (!page.fileName.empty()) {
            break;
        }
    }

    const GumboNode* extras = html::elementById(root, "i6");
    if (extras) {
        for (const GumboNode* link : html::allByTag(extras, GUMBO_TAG_A)) {
            if (html::text(link).rfind("Download original", 0) == 0) {
                page.originalUrl = html::attributeOr(link, "href");
                break;
            }
        }
    }

    std::string onerror = html::attributeOr(image, "onerror");
    const GumboNode* loadfail = html::elementById(root, "loadfail");
    std::string onclick = html::attributeOr(loadfail, "onclick");
    std::smatch match;
    if (std::regex_search(onerror, match, kReloadCall) || std::regex_search(onclick, match, kReloadCall)) {
        page.reloadToken = match[1].str();
    }

    const GumboNode* next = html::elementById(root, "next");
    if (next) {
        page.nextUrl = html::attributeOr(next, "href");
    }

    if (page.index == 0) {
        // The navigation strip reads "<span>7</span> / <span>272</span>".
        const GumboNode* strip = html::findFirst(root, [](const GumboNode* node) {
            return html::hasClass(node, "sn");
        });
        std::vector<const GumboNode*> counters = strip ? html::allByTag(strip, GUMBO_TAG_SPAN)
                                                       : std::vector<const GumboNode*>();
        if (!counters.empty()) {
            page.index = toInt(html::text(counters.front()));
        }
    }
    return true;
}

bool isBlockedResponse(const std::string& body, std::string& reason) {
    static const char* markers[] = {
        "Your IP address has been temporarily banned",
        "You have exceeded your image viewing limits",
        "This gallery has been removed or is unavailable",
        "Please wait a bit before trying again",
        "Key missing, or incorrect key provided",
        "Gallery not found",
        "This gallery is pining for the fjords",
        "This page requires you to log on",
    };
    for (const char* marker : markers) {
        if (body.find(marker) != std::string::npos) {
            reason = marker;
            return true;
        }
    }
    if (body.find("This is a temporary error") != std::string::npos) {
        reason = "temporary server error page";
        return true;
    }
    return false;
}

std::string sanitiseFileName(const std::string& name) {
    std::string cleaned;
    cleaned.reserve(name.size());
    for (char raw : name) {
        unsigned char c = static_cast<unsigned char>(raw);
        if (c < 0x20 || std::strchr("<>:\"/\\|?*", raw) != nullptr) {
            cleaned.push_back('_');
        } else {
            cleaned.push_back(raw);
        }
    }
    cleaned = trim(cleaned);
    while (!cleaned.empty() && cleaned.back() == '.') {
        cleaned.pop_back();
    }
    if (cleaned.size() > 150) {
        cleaned.resize(150);
    }
    return cleaned.empty() ? std::string("untitled") : cleaned;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    std::u8string encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string extensionOf(const std::string& url, const std::string& fallback) {
    std::string path = url;
    size_t query = path.find_first_of("?#");
    if (query != std::string::npos) {
        path.erase(query);
    }
    size_t slash = path.find_last_of('/');
    std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = leaf.find_last_of('.');
    if (dot == std::string::npos || dot + 1 == leaf.size() || leaf.size() - dot > 6) {
        return fallback;
    }
    return leaf.substr(dot);
}

std::string toJson(const Gallery& gallery) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"url\": \"" << jsonEscape(gallery.url) << "\",\n";
    json << "  \"gid\": \"" << jsonEscape(gallery.gid) << "\",\n";
    json << "  \"token\": \"" << jsonEscape(gallery.token) << "\",\n";
    json << "  \"title\": \"" << jsonEscape(gallery.titleEnglish) << "\",\n";
    json << "  \"title_jpn\": \"" << jsonEscape(gallery.titleJapanese) << "\",\n";
    json << "  \"rating\": \"" << jsonEscape(gallery.rating) << "\",\n";
    json << "  \"image_count\": " << gallery.imageCount << ",\n";
    json << "  \"details\": {";
    bool firstDetail = true;
    for (const std::pair<const std::string, std::string>& detail : gallery.details) {
        json << (firstDetail ? "\n" : ",\n");
        json << "    \"" << jsonEscape(detail.first) << "\": \"" << jsonEscape(detail.second) << "\"";
        firstDetail = false;
    }
    json << (firstDetail ? "}" : "\n  }") << ",\n";
    json << "  \"tags\": [";
    for (size_t i = 0; i < gallery.tags.size(); ++i) {
        json << (i == 0 ? "\n" : ",\n");
        json << "    { \"namespace\": \"" << jsonEscape(gallery.tags[i].ns) << "\", \"tag\": \""
             << jsonEscape(gallery.tags[i].name) << "\" }";
    }
    json << (gallery.tags.empty() ? "]" : "\n  ]") << ",\n";
    json << "  \"pages\": [";
    for (size_t i = 0; i < gallery.pages.size(); ++i) {
        json << (i == 0 ? "\n" : ",\n");
        json << "    { \"index\": " << gallery.pages[i].index << ", \"url\": \""
             << jsonEscape(gallery.pages[i].url) << "\", \"file_name\": \""
             << jsonEscape(gallery.pages[i].fileName) << "\" }";
    }
    json << (gallery.pages.empty() ? "]" : "\n  ]") << "\n";
    json << "}\n";
    return json.str();
}

}
