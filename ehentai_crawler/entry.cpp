#include <curl/curl.h>
#include <iostream>
#include <string>
#include "macros.h"

static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, total);
    return total; 
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "couldn't init curl\n";
        return 1;
    }

    std::string html;
    curl_easy_setopt(curl, CURLOPT_URL, "https://e-hentai.org/g/3264912/be4872163f/");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); 
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "request failed: " << curl_easy_strerror(res) << "\n";
    }
    else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}