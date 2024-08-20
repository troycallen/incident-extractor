#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <filesystem>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct ShootingInfo {
    std::string date;
    int victims;
    std::string location;
    std::string description;
    std::string source;
};

std::string performOCR(const std::string& imagePath) {
    tesseract::TessBaseAPI tess;
    if (tess.Init(NULL, "eng")) {
        std::cerr << "Could not initialize tesseract." << std::endl;
        return "";
    }
    Pix *image = pixRead(imagePath.c_str());
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
        "massacre", "massacred"
    };

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

    std::regex victimsPattern(R"((\d+)\s+(?:people|individuals|persons)\s+(?:killed|dead|fatally shot))");
    std::smatch victimsMatch;
    if (std::regex_search(text, victimsMatch, victimsPattern)) {
        info.victims = std::stoi(victimsMatch[1]);
    }

    std::regex locationPattern(R"(in\s+([A-Z][a-z]+(?:,\s+[A-Z]{2})?))");
    std::smatch locationMatch;
    if (std::regex_search(text, locationMatch, locationPattern)) {
        info.location = locationMatch[1];
    }

    info.description = text.substr(0, 500); // First 500 characters as description
    return info;
}

std::vector<ShootingInfo> processNewspaperImages(const std::string& folderPath) {
    std::vector<ShootingInfo> data;
    for (const auto & entry : fs::directory_iterator(folderPath)) {
        if (entry.path().extension() == ".png" || entry.path().extension() == ".jpg" || 
            entry.path().extension() == ".jpeg" || entry.path().extension() == ".tiff") {
            std::string ocrText = performOCR(entry.path().string());
            if (containsRelevantTerms(ocrText)) {
                ShootingInfo info = extractShootingInfo(ocrText, entry.path().filename().string());
                if (info.victims > 0 && !info.location.empty()) {  // Basic filtering
                    data.push_back(info);
                }
            }
        }
    }
    return data;
}

void saveToJson(const std::vector<ShootingInfo>& data, const std::string& filename) {
    json j;
    for (const auto& info : data) {
        j.push_back({
            {"date", info.date},
            {"victims", info.victims},
            {"location", info.location},
            {"description", info.description},
            {"source", info.source}
        });
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