#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "crawler.h"
#include "http.h"
#include "macros.h"

namespace {

void printUsage() {
    std::cout <<
        "ehentai_crawler - download a gallery from e-hentai/exhentai\n"
        "\n"
        "usage: ehentai_crawler <gallery-url> [options]\n"
        "\n"
        "options:\n"
        "  -o, --output <dir>    where to put the files (default: " DEFAULT_OUTPUT_DIR ")\n"
        "  -c, --cookies <str>   cookie header, needed for exhentai and member settings\n"
        "                        e.g. \"ipb_member_id=...; ipb_pass_hash=...; igneous=...\"\n"
        "  -j, --jobs <n>        parallel workers (default: " << DEFAULT_JOBS << ")\n"
        "  -d, --delay <ms>      minimum delay between requests (default: " << DEFAULT_DELAY_MS << ")\n"
        "  -r, --retries <n>     retries per request (default: " << DEFAULT_RETRIES << ")\n"
        "      --from <n>        first page to fetch, 1 based (default: 1)\n"
        "      --to <n>          last page to fetch (default: the last one)\n"
        "      --original        grab the original upload instead of the resample\n"
        "      --metadata-only   write metadata.json and stop\n"
        "      --overwrite       refetch images that are already on disk\n"
        "      --flat            write into the output dir without a per gallery folder\n"
        "      --proxy <url>     route every request through this proxy\n"
        "      --user-agent <s>  override the user agent\n"
        "  -q, --quiet           only print errors\n"
        "  -h, --help            show this help\n"
        "\n"
        "exit codes: 0 all good, 1 something failed, 2 the site cut the crawl short\n";
}

bool needsValue(const std::string& flag, int index, int argc) {
    if (index + 1 < argc) {
        return true;
    }
    std::cerr << flag << " needs a value\n";
    return false;
}

bool parseInt(const std::string& flag, const std::string& raw, int& out) {
    try {
        size_t consumed = 0;
        int value = std::stoi(raw, &consumed);
        if (consumed != raw.size()) {
            throw std::invalid_argument(raw);
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        std::cerr << flag << " wants a number, got \"" << raw << "\"\n";
        return false;
    }
}

bool parseArguments(int argc, char** argv, CrawlerConfig& config, bool& showHelp) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            showHelp = true;
            return true;
        } else if (arg == "-o" || arg == "--output") {
            if (!needsValue(arg, i, argc)) return false;
            config.outputDir = argv[++i];
        } else if (arg == "-c" || arg == "--cookies") {
            if (!needsValue(arg, i, argc)) return false;
            config.cookies = argv[++i];
        } else if (arg == "-j" || arg == "--jobs") {
            if (!needsValue(arg, i, argc) || !parseInt(arg, argv[++i], config.jobs)) return false;
        } else if (arg == "-d" || arg == "--delay") {
            if (!needsValue(arg, i, argc) || !parseInt(arg, argv[++i], config.delayMs)) return false;
        } else if (arg == "-r" || arg == "--retries") {
            if (!needsValue(arg, i, argc) || !parseInt(arg, argv[++i], config.retries)) return false;
        } else if (arg == "--from") {
            if (!needsValue(arg, i, argc) || !parseInt(arg, argv[++i], config.firstPage)) return false;
        } else if (arg == "--to") {
            if (!needsValue(arg, i, argc) || !parseInt(arg, argv[++i], config.lastPage)) return false;
        } else if (arg == "--proxy") {
            if (!needsValue(arg, i, argc)) return false;
            config.proxy = argv[++i];
        } else if (arg == "--user-agent") {
            if (!needsValue(arg, i, argc)) return false;
            config.userAgent = argv[++i];
        } else if (arg == "--original") {
            config.original = true;
        } else if (arg == "--metadata-only") {
            config.metadataOnly = true;
        } else if (arg == "--overwrite") {
            config.overwrite = true;
        } else if (arg == "--flat") {
            config.flat = true;
        } else if (arg == "-q" || arg == "--quiet") {
            config.quiet = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        } else if (config.url.empty()) {
            config.url = arg;
        } else {
            std::cerr << "only one gallery url at a time, got an extra: " << arg << "\n";
            return false;
        }
    }
    return true;
}

bool validate(const CrawlerConfig& config) {
    if (config.url.empty()) {
        std::cerr << "no gallery url given, try --help\n";
        return false;
    }
    if (config.jobs < 1) {
        std::cerr << "--jobs must be at least 1\n";
        return false;
    }
    if (config.delayMs < 0) {
        std::cerr << "--delay can't be negative\n";
        return false;
    }
    if (config.retries < 0) {
        std::cerr << "--retries can't be negative\n";
        return false;
    }
    if (config.firstPage < 1) {
        std::cerr << "--from starts at 1\n";
        return false;
    }
    if (config.lastPage != 0 && config.lastPage < config.firstPage) {
        std::cerr << "--to must not be smaller than --from\n";
        return false;
    }
    return true;
}

}

int main(int argc, char** argv) {
    CrawlerConfig config;
    bool showHelp = false;
    if (!parseArguments(argc, argv, config, showHelp)) {
        return 1;
    }
    if (showHelp || argc == 1) {
        printUsage();
        return showHelp ? 0 : 1;
    }
    if (!validate(config)) {
        return 1;
    }

    if (!http::globalInit()) {
        std::cerr << "couldn't init curl\n";
        return 1;
    }

    Crawler crawler(config);
    int status = crawler.run();

    http::globalCleanup();
    return status;
}
