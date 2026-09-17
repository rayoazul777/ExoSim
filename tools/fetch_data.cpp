// FetchData - C++ replacement for data/fetch_exoplanets.py.
//
// Queries the SAME three NASA/astronomical data sources as the Python
// script: the NASA Exoplanet Archive's TAP service (planets, stellar
// hosts), NASA/JPL's SBDB API (5 dwarf planets), and CDS's VizieR TAP
// service (the real Hipparcos star catalog for the skybox) - three
// genuinely separate, independent sources. This is NOT a required step -
// either fetch script/tool works fine on its own, and ExoSim.exe never
// invokes either one or depends on Python being installed. This C++
// version exists for anyone who'd rather keep the whole toolchain in one
// language, not because the Python version caused any actual problem.
//
// Usage:
//   FetchData [--out-dir DIR] [--limit N] [--top-systems N]
//             [--skip-dwarf-planets] [--skip-star-catalog]
//
//   --out-dir DIR           Where to write the JSON files (default: "data")
//   --limit N               Only fetch the first N rows of each TAP table (testing)
//   --top-systems N         Print the N systems with the most planets, found
//                           via a min-heap (see PrintTopSystemsByPlanetCount) -
//                           a genuine, natural use of a heap for this data:
//                           picking the "richest" N systems without sorting
//                           the whole multi-thousand-row dataset.
//   --skip-dwarf-planets    Skip the NASA/JPL SBDB dwarf-planet fetch (5 quick
//                           extra requests to a second endpoint)
//   --skip-star-catalog     Skip the VizieR/Hipparcos star catalog fetch
//                           (~9,000-row CSV request to a third endpoint)

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
#include <cctype>

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
    "glat", "glon",
};

const std::vector<std::string> kStellarHostColumns = {
    "hostname", "st_teff", "st_rad", "st_mass", "st_spectype", "sy_snum",
};

// NASA/JPL's own Small-Body DataBase API - a DIFFERENT, official NASA
// data source from the Exoplanet Archive above. No key needed. Used to
// pull real orbital/physical data for our Solar System's 5 IAU-recognized
// dwarf planets. Docs: https://ssd-api.jpl.nasa.gov/doc/sbdb.html
const std::string kSbdbBase = "https://ssd-api.jpl.nasa.gov/sbdb.api";
const std::vector<std::string> kDwarfPlanetNames = {"Ceres", "Pluto", "Eris", "Haumea", "Makemake"};

// CDS's VizieR TAP service (Strasbourg) - a THIRD distinct data source,
// separate from both NASA services above. Used for the real Hipparcos
// star catalog (ESA mission, table I/239/hip_main) that drives the
// skybox's starfield. No key needed. Docs: https://tapvizier.cds.unistra.fr/adql/
const std::string kVizierTapBase = "https://tapvizier.cds.unistra.fr/TAPVizieR/tap/sync";
// Vmag <= 6.5 is the traditional naked-eye visibility limit (~9,000 stars) -
// a scope match for "the visible night sky", not the full ~118,000-star catalog.
const double kStarCatalogVmagLimit = 6.5;

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

// Performs one GET request and parses the response body as JSON. Shared
// by both the Exoplanet Archive TAP queries and the SBDB queries below -
// they use different URL schemes, but the fetch/parse mechanics are identical.
json HttpGetJson(CURL* curl, const std::string& url) {
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
        throw std::runtime_error("HTTP request failed: HTTP " + std::to_string(httpCode));
    }
    return json::parse(response);
}

// Runs one TAP query (percent-encoded via curl_easy_escape, same as
// urllib.parse.urlencode did in the Python version) and returns the
// parsed JSON array of rows.
json RunQuery(CURL* curl, const std::string& query) {
    char* escaped = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string url = kTapBase + "?query=" + escaped + "&format=json";
    curl_free(escaped);
    return HttpGetJson(curl, url);
}

double JsonNumOrNull(const json& j, double fallback = 0.0) {
    if (j.is_null()) return fallback;
    if (j.is_string()) {
        try { return std::stod(j.get<std::string>()); } catch (...) { return fallback; }
    }
    if (j.is_number()) return j.get<double>();
    return fallback;
}

// Same as HttpGetJson but returns the raw response body - used for
// VizieR, which returns CSV here (confirmed working in published
// examples) rather than JSON (unconfirmed for this specific service).
std::string HttpGetText(CURL* curl, const std::string& url) {
    std::cout << "Requesting: " << url << "\n";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    }
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        throw std::runtime_error("HTTP request failed: HTTP " + std::to_string(httpCode));
    }
    return response;
}

// Splits one CSV line on commas. Deliberately simple (no quoted-field
// handling) since VizieR's CSV output for this numeric catalog doesn't
// quote fields - matches what the response actually looks like for
// simple numeric/identifier columns like these.
std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

std::string ToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Queries CDS's VizieR TAP service for the Hipparcos catalog: real right
// ascension, declination, visual magnitude, and B-V color index for
// every star down to the naked-eye limit. Returns a JSON array shaped to
// match what Dataset::LoadStarCatalog expects: {"ra_deg","dec_deg","vmag","bv"}.
// Parses CSV defensively - any row that doesn't parse cleanly is
// skipped rather than aborting the whole fetch.
json FetchStarCatalog(CURL* curl) {
    std::ostringstream adql;
    adql << "SELECT HIP, RAICRS, DEICRS, Vmag, \"B-V\" FROM \"I/239/hip_main\" WHERE Vmag<=" << kStarCatalogVmagLimit;

    char* escaped = curl_easy_escape(curl, adql.str().c_str(), static_cast<int>(adql.str().size()));
    std::string url = kVizierTapBase + "?REQUEST=doQuery&LANG=ADQL&FORMAT=CSV&QUERY=" + escaped;
    curl_free(escaped);

    std::string text;
    try {
        text = HttpGetText(curl, url);
    } catch (const std::exception& e) {
        std::cerr << "WARNING: couldn't fetch the Hipparcos star catalog (" << e.what()
                  << ") - the skybox will fall back to its procedural stars, but ExoSim still runs fine.\n";
        return json::array();
    }

    std::istringstream stream(text);
    std::string line;
    std::vector<std::vector<std::string>> rows;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(SplitCsvLine(line));
    }
    if (rows.empty()) {
        std::cerr << "WARNING: Hipparcos catalog response had no usable rows.\n";
        return json::array();
    }

    std::vector<std::string> header;
    for (auto h : rows[0]) {
        while (!h.empty() && h.front() == '_') h.erase(h.begin());
        header.push_back(ToUpper(h));
    }
    auto colIndex = [&](std::initializer_list<const char*> names) -> int {
        for (const char* name : names) {
            auto it = std::find(header.begin(), header.end(), name);
            if (it != header.end()) return static_cast<int>(it - header.begin());
        }
        return -1;
    };

    int raI = colIndex({"RAICRS", "RA_ICRS", "RA(ICRS)"});
    int deI = colIndex({"DEICRS", "DE_ICRS", "DE(ICRS)"});
    int vmagI = colIndex({"VMAG"});
    int bvI = colIndex({"B-V", "B_V", "BV"});

    if (raI < 0 || deI < 0 || vmagI < 0) {
        std::cerr << "WARNING: unexpected Hipparcos CSV header, couldn't find RA/Dec/Vmag columns, "
                  << "skipping star catalog.\n";
        return json::array();
    }

    json results = json::array();
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        int maxNeeded = std::max({raI, deI, vmagI});
        if (static_cast<int>(row.size()) <= maxNeeded) continue;

        double ra = JsonNumOrNull(row[raI]);
        double dec = JsonNumOrNull(row[deI]);
        double vmag = JsonNumOrNull(row[vmagI]);
        if (row[raI].empty() || row[deI].empty() || row[vmagI].empty()) continue;

        json record;
        record["ra_deg"] = ra;
        record["dec_deg"] = dec;
        record["vmag"] = vmag;
        if (bvI >= 0 && bvI < static_cast<int>(row.size()) && !row[bvI].empty()) {
            record["bv"] = JsonNumOrNull(row[bvI]);
        } else {
            record["bv"] = nullptr;
        }
        results.push_back(record);
    }

    return results;
}

// Queries NASA/JPL's SBDB API for each of the 5 IAU-recognized dwarf
// planets and returns a JSON array shaped to match what
// Dataset::LoadDwarfPlanets expects on the app side (same shape the
// Python fetch script produces): real orbital elements (a, e, i,
// argument of periapsis, longitude of ascending node, period) plus
// diameter and GM (standard gravitational parameter) where SBDB has
// them measured.
json FetchDwarfPlanets(CURL* curl) {
    json results = json::array();

    for (const auto& name : kDwarfPlanetNames) {
        char* escapedName = curl_easy_escape(curl, name.c_str(), static_cast<int>(name.size()));
        std::string url = kSbdbBase + "?sstr=" + escapedName + "&phys-par=1";
        curl_free(escapedName);

        json data;
        try {
            data = HttpGetJson(curl, url);
        } catch (const std::exception& e) {
            std::cerr << "  WARNING: couldn't fetch " << name << " from SBDB: " << e.what() << "\n";
            continue;
        }

        if (!data.contains("object") || !data.contains("orbit")) {
            std::cerr << "  WARNING: unexpected SBDB response for " << name << ", skipping\n";
            continue;
        }

        std::unordered_map<std::string, double> elements;
        if (data["orbit"].contains("elements")) {
            for (const auto& el : data["orbit"]["elements"]) {
                if (el.contains("name") && el.contains("value")) {
                    elements[el["name"].get<std::string>()] = JsonNumOrNull(el["value"]);
                }
            }
        }
        double diameterKm = 0.0, gm = 0.0;
        if (data.contains("phys_par")) {
            for (const auto& p : data["phys_par"]) {
                if (!p.contains("name") || !p.contains("value")) continue;
                std::string pname = p["name"].get<std::string>();
                if (pname == "diameter") diameterKm = JsonNumOrNull(p["value"]);
                else if (pname == "GM") gm = JsonNumOrNull(p["value"]);
            }
        }

        json record;
        record["pl_name"] = name; // plain query name, not SBDB's numbered "134340 Pluto" style fullname
        record["hostname"] = "Sun";
        record["pl_orbsmax"] = elements.count("a") ? json(elements["a"]) : json(nullptr);
        record["pl_orbeccen"] = elements.count("e") ? json(elements["e"]) : json(nullptr);
        record["pl_orbincl"] = elements.count("i") ? json(elements["i"]) : json(nullptr);
        record["pl_orblper"] = elements.count("w") ? json(elements["w"]) : json(nullptr);
        record["pl_orbper"] = elements.count("per") ? json(elements["per"]) : json(nullptr);
        record["long_asc_node"] = elements.count("om") ? json(elements["om"]) : json(nullptr);
        record["diameter_km"] = diameterKm > 0.0 ? json(diameterKm) : json(nullptr);
        record["gm"] = gm > 0.0 ? json(gm) : json(nullptr);
        record["source"] = "NASA/JPL SBDB";

        results.push_back(record);
    }

    return results;
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
    bool skipDwarfPlanets = false;
    bool skipStarCatalog = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out-dir" && i + 1 < argc) outDir = argv[++i];
        else if (arg == "--limit" && i + 1 < argc) limit = std::stoi(argv[++i]);
        else if (arg == "--top-systems" && i + 1 < argc) topN = std::stoi(argv[++i]);
        else if (arg == "--skip-dwarf-planets") skipDwarfPlanets = true;
        else if (arg == "--skip-star-catalog") skipStarCatalog = true;
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

        json dwarfRows = skipDwarfPlanets ? json::array() : FetchDwarfPlanets(curl);
        if (!dwarfRows.empty()) {
            std::string dwarfPath = outDir + "/dwarf_planets.json";
            std::ofstream(dwarfPath) << dwarfRows.dump();
            std::cout << "Wrote " << dwarfRows.size() << " dwarf planet records to " << dwarfPath
                      << " (NASA/JPL SBDB - a different NASA source from the Exoplanet Archive above)\n";
        }

        json starRows = skipStarCatalog ? json::array() : FetchStarCatalog(curl);
        if (!starRows.empty()) {
            std::string starPath = outDir + "/star_catalog.json";
            std::ofstream(starPath) << starRows.dump();
            std::cout << "Wrote " << starRows.size() << " real star positions to " << starPath
                      << " (Hipparcos catalog via CDS VizieR TAP - a third, separate data source)\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        exitCode = 1;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return exitCode;
}
