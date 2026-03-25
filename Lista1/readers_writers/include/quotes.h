#pragma once
#ifndef PRR_QUOTES_H
#define PRR_QUOTES_H

#include <string>
#include <ctime>
#include <curl/curl.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string getQuote() {
    CURL* curl;
    CURLcode res;
    std::string readBuffer = "";

    curl = curl_easy_init();
    if(curl) {
        std::string url = "https://api.adviceslip.com/advice";

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if(res == CURLE_OK) {
            try {
                auto j = json::parse(readBuffer);
                auto slip = j["slip"];
                std::string advice = slip["advice"].get<std::string>();

                return advice;

            } catch (const std::exception& e) {
                return "JSON Error: " + std::string(e.what()) + " | Buffer: " + readBuffer;
            }
        }
    }
    return "Connection error.";
}

#endif //PRR_QUOTES_H
