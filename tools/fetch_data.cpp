// FetchData - C++ replacement for data/fetch_exoplanets.py.
//
// Queries the NASA Exoplanet Archive's TAP service (same endpoint, same
// ADQL queries) and writes data/exoplanets.json and data/stellar_hosts.json,
// exactly like the Python script did. This is NOT a required step - the
// Python script works fine and runs completely separately from ExoSim.exe
// (ExoSim never invokes Python or depends on it being installed). This
// exists because keeping the whole toolchain in one language is a
// reasonable preference, not because the Python version caused any actual
// problem.
//
// Usage:
//   FetchData [--out-dir DIR] [--limit N] [--top-systems N]
//
//   --out-dir DIR       Where to write the JSON files (default: "data")
//   --limit N           Only fetch the first N rows of each table (testing)
//   --top-systems N     Print the N systems with the most planets, found
//                       via a min-heap (see PrintTopSystemsByPlanetCount) -
//                       a genuine, natural use of a heap for this data:
//                       picking the "richest" N systems without sorting
//                       the whole multi-thousand-row dataset.

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

using json = nlohmann::json;

namespace {

const std::string kTapBase = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync";

// Same columns as fetch_exoplanets.py's COLUMNS / STELLARHOSTS_COLUMNS -
// see that file's comments for why each one is here.
const std::vector<std::string> kPlanetColumns = {
    "pl_name", "hostname",
    "pl_orbper", "pl_orbsmax", "pl_orbeccen", "pl_orbincl", "pl_orblper",
    "pl_bmassj", "pl_radj",
    "pl_dens", "pl_eqt", "pl_insol",
    "st_mass", "st_rad", "st_teff", "st_spectype",
    "sy_snum",
    "sy_dist", "ra", "dec",
};

const std::vector<std::string> kStellarHostColumns = {
    "hostname", "st_teff", "st_rad", "st_mass", "st_spectype", "sy_snum",
};

size_t WriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(contents, total);
    return total;
}

std::string JoinColumns(const std::vector<std::string>& cols) {
    std::ostringstream oss;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i) oss << ",";
        oss << cols[i];
    }
    return oss.str();
}

std::string BuildQuery(const std::string& table, const std::vector<std::string>& cols,
                        const std::string& whereClause, int limit) {
    std::ostringstream oss;
    oss << "SELECT ";
    if (limit > 0) oss << "TOP " << limit << " ";
    oss << JoinColumns(cols) << " FROM " << table;
    if (!whereClause.empty()) oss << " WHERE " << whereClause;
    return oss.str();
}

// Runs one TAP query (percent-encoded via curl_easy_escape, same as
// urllib.parse.urlencode did in the Python version) and returns the
// parsed JSON array of rows.
json RunQuery(CURL* curl, const std::string& query) {
    char* escaped = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string url = kTapBase + "?query=" + escaped + "&format=json";
    curl_free(escaped);

    std::cout << "Requesting: " << url << "\n";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    }
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        throw std::runtime_error("TAP request failed: HTTP " + std::to_string(httpCode));
    }
    return json::parse(response);
}

// Finds the N systems with the most planets using a min-heap of size N:
// push every (count, hostname) pair, and once the heap exceeds N entries,
// pop the smallest. Only the N largest survive - the classic heap-based
// "top-K" selection algorithm, and a genuinely natural fit here (we want
// the K richest systems, not a full sort of every system in the archive).
void PrintTopSystemsByPlanetCount(const json& planetRows, int topN) {
    std::unordered_map<std::string, int> countByHost;
    for (const auto& row : planetRows) {
        if (!row.contains("hostname") || row["hostname"].is_null()) continue;
        countByHost[row["hostname"].get<std::string>()]++;
    }

    using Entry = std::pair<int, std::string>; // (planetCount, hostname)
    // std::greater<> makes this a MIN-heap ordered by count, so the
    // smallest of the current top-N sits at the top and gets evicted
    // first when a richer system comes along.
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> minHeap;

    for (const auto& kv : countByHost) {
        minHeap.push({kv.second, kv.first});
        if (static_cast<int>(minHeap.size()) > topN) {
            minHeap.pop();
        }
    }

    std::vector<Entry> top;
    while (!minHeap.empty()) {
        top.push_back(minHeap.top());
        minHeap.pop();
    }
    std::reverse(top.begin(), top.end()); // largest first

    std::cout << "Top " << top.size() << " systems by planet count (via min-heap selection):\n";
    for (const auto& entry : top) {
        std::cout << "  " << entry.second << " - " << entry.first << " planets\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string outDir = "data";
    int limit = 0;
    int topN = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out-dir" && i + 1 < argc) outDir = argv[++i];
        else if (arg == "--limit" && i + 1 < argc) limit = std::stoi(argv[++i]);
        else if (arg == "--top-systems" && i + 1 < argc) topN = std::stoi(argv[++i]);
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize libcurl\n";
        curl_global_cleanup();
        return 1;
    }

    int exitCode = 0;
    try {
        json planetRows = RunQuery(curl, BuildQuery("ps", kPlanetColumns, "default_flag=1", limit));

        std::string planetPath = outDir + "/exoplanets.json";
        std::ofstream(planetPath) << planetRows.dump();

        int usable = 0;
        for (const auto& r : planetRows) {
            bool hasA = r.contains("pl_orbsmax") && !r["pl_orbsmax"].is_null();
            bool hasP = r.contains("pl_orbper") && !r["pl_orbper"].is_null();
            if (hasA && hasP) ++usable;
        }
        std::cout << "Wrote " << planetRows.size() << " planet records to " << planetPath << "\n";
        std::cout << "(" << usable << " have both semi-major axis and period, so are drawable)\n";

        if (topN > 0) PrintTopSystemsByPlanetCount(planetRows, topN);

        try {
            json stellarRows = RunQuery(curl, BuildQuery("stellarhosts", kStellarHostColumns, "", limit));
            std::string stellarPath = outDir + "/stellar_hosts.json";
            std::ofstream(stellarPath) << stellarRows.dump();
            std::cout << "Wrote " << stellarRows.size() << " stellar host records to " << stellarPath << "\n";
        } catch (const std::exception& e) {
            std::cerr << "WARNING: couldn't fetch stellar hosts table (" << e.what()
                      << ") - companion stars won't be modeled, but ExoSim still runs fine without it.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        exitCode = 1;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return exitCode;
}
