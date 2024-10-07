#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct ShootingInfo {
    std::string date;
    int victims;
    std::string location;
    std::string description;
    std::string source;
};

std::mutex mtx;

std::string performOCR(const std::string& imagePath) {
    tesseract::TessBaseAPI tess;
    if (tess.Init(NULL, "eng", tesseract::OEM_LSTM_ONLY)) {
        std::cerr << "Could not initialize tesseract." << std::endl;
        return "";
    }

    // Set Page Segmentation Mode to Auto
    tess.SetPageSegMode(tesseract::PSM_AUTO);

    // Set image processing variables
    tess.SetVariable("image_default_resolution", "300");
    tess.SetVariable("tessedit_char_whitelist", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?-_'\"()");

    Pix* image = pixRead(imagePath.c_str());
    tess.SetImage(image);

    char* outText = tess.GetUTF8Text();
    std::string result(outText);
    delete[] outText;
    pixDestroy(&image);

    return result;
}

std::string extractDate(const std::string& text) {
    std::regex datePattern(R"(\b(?:January|February|March|April|May|June|July|August|September|October|November|December)\s+\d{1,2},\s+\d{4}\b)");
    std::smatch match;
    if (std::regex_search(text, match, datePattern)) {
        return match.str();
    }
    return "";
}

bool containsRelevantTerms(const std::string& text) {
    static const std::unordered_set<std::string> terms = {
        "multiple counts", "multiple dead", "multiple homicide", "multiple murder", "multiple shot",
        "murder", "murdered", "murdering", "murderer", "murder suicide", "quadruple homicide",
        "quadruple murder", "rage", "rampage", "retaliation", "revenge", "rifle", "serial killer",
        "serial murder", "shoot", "shooter", "shooting", "shot", "shot dead", "shotgun", "slain",
        "slay", "slayed", "slaying", "slaughter", "slaughtered", "spree", "stand-off", "standoff",
        "suicide", "suspect dead", "tragedy", "tragic", "wound", "wounded", "wounding", "altercation",
        "bullet", "bullets", "casing", "casings", "dead", "deadly", "death", "deaths", "death penalty",
        "death sentence", "domestic", "dispute", "drive-by", "drug related", "erupted", "execution",
        "executed", "family killing", "family murder", "fatal", "fatality", "fatalities", "gun",
        "gunfire", "guns", "gunman", "gunmen", "gunned down", "gunshot", "handgun", "heinous",
        "kill", "killed", "killing", "killer", "life sentence", "mass murder", "mass shooting",
        "massacre", "massacred"};

    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

    for (const auto& term : terms) {
        if (lowerText.find(term) != std::string::npos) {
            return true;
        }
    }
    return false;
}

ShootingInfo extractShootingInfo(const std::string& text, const std::string& source) {
    ShootingInfo info;
    info.source = source;
    info.date = extractDate(text);

    // Improved victim count extraction
    std::regex victimsPattern(R"((\d+)\s*(?:people|individuals|persons|victims|killed|dead|fatally shot|injured))");
    std::smatch victimsMatch;
    if (std::regex_search(text, victimsMatch, victimsPattern)) {
        info.victims = std::stoi(victimsMatch[1]);
    }

    // Improved location extraction
    std::regex locationPattern(R"(in\s+((?:[A-Z][a-z]+\s*)+(?:,\s*[A-Z]{2})?))");
    std::smatch locationMatch;
    if (std::regex_search(text, locationMatch, locationPattern)) {
        info.location = locationMatch[1];
    }

    // Extract additional details
    std::regex detailsPattern(R"(((?:mass\s+shooting|shooting|incident).*?(?:\.|\n)))");
    std::smatch detailsMatch;
    if (std::regex_search(text, detailsMatch, detailsPattern)) {
        info.description = detailsMatch[1];
    } else {
        info.description = text.substr(0, 500);  // Fallback to first 500 characters
    }

    return info;
}

void processImagesThread(const std::vector<fs::path>& imagePaths, std::vector<ShootingInfo>& data, size_t start, size_t end) {
    for (size_t i = start; i < end; ++i) {
        std::string ocrText = performOCR(imagePaths[i].string());
        if (containsRelevantTerms(ocrText)) {
            ShootingInfo info = extractShootingInfo(ocrText, imagePaths[i].filename().string());
            if (info.victims > 0 && !info.location.empty()) {
                std::lock_guard<std::mutex> lock(mtx);
                data.push_back(info);
            }
        }
    }
}

std::vector<ShootingInfo> processNewspaperImages(const std::string& folderPath) {
    std::vector<fs::path> imagePaths;
    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (entry.path().extension() == ".png" || entry.path().extension() == ".jpg" ||
            entry.path().extension() == ".jpeg" || entry.path().extension() == ".tiff") {
            imagePaths.push_back(entry.path());
        }
    }

    std::vector<ShootingInfo> data;
    size_t numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    size_t imagesPerThread = imagePaths.size() / numThreads;

    for (size_t i = 0; i < numThreads; ++i) {
        size_t start = i * imagesPerThread;
        size_t end = (i == numThreads - 1) ? imagePaths.size() : (i + 1) * imagesPerThread;
        threads.emplace_back(processImagesThread, std::ref(imagePaths), std::ref(data), start, end);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    return data;
}

void saveToJson(const std::vector<ShootingInfo>& data, const std::string& filename) {
    json j;
    for (const auto& info : data) {
        j.push_back({{"date", info.date},
                     {"victims", info.victims},
                     {"location", info.location},
                     {"description", info.description},
                     {"source", info.source}});
    }
    std::ofstream o(filename);
    o << std::setw(4) << j << std::endl;
}

int main() {
    std::string folderPath = "C:\\Users\\burtt\\Documents\\DMS_Research\\Incidents";
    auto shootingData = processNewspaperImages(folderPath);
    saveToJson(shootingData, "mass_shootings_database.json");
    std::cout << "Total events recorded: " << shootingData.size() << std::endl;
    return 0;
}