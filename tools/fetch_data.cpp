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
//   --star-vmag-limit M     Faintest star magnitude to fetch (default 6.5 = naked eye,
//                           ~9k stars; 8.0 ~ 40k; 12.4 = full Hipparcos, ~118k)
//   --skip-double-stars     Skip the Washington Double Star catalog fetch
//   --skip-star-catalog     Skip the VizieR/Hipparcos star catalog fetch
//                           (~9,000-row CSV request to a third endpoint)

// windows.h (pulled in transitively by <curl/curl.h> on Windows) defines
// max/min as macros unless told not to - that silently mangles every
// std::max/std::min call in this file (std::max({raI, deI, vmagI}) below
// being the one that actually gets hit), which is why fixing THIS is what
// clears a whole cascade of otherwise-nonsensical-looking parse errors.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

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
#include <cmath>

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
// verifySsl=false disables certificate verification for this one call -
// only ever used as a scoped fallback after a genuine verification
// failure (see FetchStarCatalog), not a default.
std::string HttpGetText(CURL* curl, const std::string& url, bool verifySsl = true) {
    std::cout << "Requesting: " << url << "\n";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verifySsl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verifySsl ? 2L : 0L);
#ifdef CURLSSLOPT_NATIVE_CA
    // Trust the OS's own certificate store (curl >= 7.71). On Windows this is
    // the proper fix for "unable to get local issuer certificate" errors, so
    // the insecure fallback below should rarely, if ever, be needed.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif

    CURLcode res = curl_easy_perform(curl);
    // Reset to the secure default immediately so an insecure fallback
    // here never silently leaks into a later, unrelated request reusing
    // this same CURL handle.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

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

// Runs one ADQL query against CDS VizieR's TAP service and returns parsed
// CSV rows (header first), or empty on failure. Certificate handling:
// secure (with the OS-native CA store where curl supports it) first, then
// ONE retry without verification - only after a genuine certificate error,
// only for this public, non-sensitive astronomical data, always logged.
std::vector<std::vector<std::string>> VizierCsvRows(CURL* curl, const std::string& adql, const std::string& label) {
    char* escaped = curl_easy_escape(curl, adql.c_str(), static_cast<int>(adql.size()));
    std::string url = kVizierTapBase + "?REQUEST=doQuery&LANG=ADQL&FORMAT=CSV&QUERY=" + escaped;
    curl_free(escaped);

    std::string text;
    try {
        text = HttpGetText(curl, url);
    } catch (const std::exception& e) {
        std::string what = e.what();
        bool certError = what.find("certificate") != std::string::npos || what.find("SSL") != std::string::npos;
        if (!certError) {
            std::cerr << "WARNING: couldn't fetch " << label << " (" << what << ").\n";
            return {};
        }
        std::cerr << "WARNING: TLS certificate verification failed for VizieR (" << what
                  << ") - retrying this one public-data request without verification.\n";
        try {
            text = HttpGetText(curl, url, /*verifySsl=*/false);
        } catch (const std::exception& e2) {
            std::cerr << "WARNING: retry for " << label << " also failed (" << e2.what() << ").\n";
            return {};
        }
    }

    std::vector<std::vector<std::string>> rows;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(SplitCsvLine(line));
    }
    if (rows.size() < 2) {
        std::cerr << "WARNING: " << label << " response had no data rows. First 300 chars:\n"
                  << text.substr(0, 300) << "\n";
        return {};
    }
    for (auto& h : rows[0]) {
        while (!h.empty() && (h.front() == '_' || h.front() == ' ')) h.erase(h.begin());
        while (!h.empty() && h.back() == ' ') h.pop_back();
        h = ToUpper(h);
    }
    return rows;
}

int ColIndex(const std::vector<std::string>& header, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        auto it = std::find(header.begin(), header.end(), name);
        if (it != header.end()) return static_cast<int>(it - header.begin());
    }
    return -1;
}

bool CellNum(const std::vector<std::string>& row, int i, double& out) {
    if (i < 0 || i >= static_cast<int>(row.size()) || row[i].empty()) return false;
    try { out = std::stod(row[i]); return true; } catch (...) { return false; }
}

// Real Hipparcos catalog (ESA; VizieR I/239/hip_main): position, magnitude,
// B-V color, proper motion (pmRA includes cos(dec)) and parallax, for every
// star down to `vmagLimit`. Shape matches Dataset::LoadStarCatalog.
json FetchStarCatalog(CURL* curl, double vmagLimit) {
    std::ostringstream adql;
    adql << "SELECT HIP, RAICRS, DEICRS, Vmag, \"B-V\", pmRA, pmDE, Plx FROM \"I/239/hip_main\" WHERE Vmag<=" << vmagLimit;
    auto rows = VizierCsvRows(curl, adql.str(), "the Hipparcos star catalog");
    if (rows.empty()) return json::array();
    const auto& h = rows[0];
    int raI = ColIndex(h, {"RAICRS", "RA_ICRS", "RA(ICRS)"}), deI = ColIndex(h, {"DEICRS", "DE_ICRS", "DE(ICRS)"});
    int vmI = ColIndex(h, {"VMAG"}), bvI = ColIndex(h, {"B-V", "B_V", "BV"});
    int prI = ColIndex(h, {"PMRA"}), pdI = ColIndex(h, {"PMDE"}), pxI = ColIndex(h, {"PLX"});
    if (raI < 0 || deI < 0 || vmI < 0) {
        std::cerr << "WARNING: unexpected Hipparcos CSV header - skipping star catalog.\n";
        return json::array();
    }
    json out = json::array();
    for (size_t r = 1; r < rows.size(); ++r) {
        double ra, dec, vmag, v;
        if (!CellNum(rows[r], raI, ra) || !CellNum(rows[r], deI, dec) || !CellNum(rows[r], vmI, vmag)) continue;
        json rec{{"ra_deg", ra}, {"dec_deg", dec}, {"vmag", vmag}};
        rec["bv"] = CellNum(rows[r], bvI, v) ? json(v) : json(nullptr);
        rec["pm_ra_mas_yr"] = CellNum(rows[r], prI, v) ? json(v) : json(nullptr);
        rec["pm_dec_mas_yr"] = CellNum(rows[r], pdI, v) ? json(v) : json(nullptr);
        rec["parallax_mas"] = CellNum(rows[r], pxI, v) ? json(v) : json(nullptr);
        out.push_back(rec);
    }
    return out;
}

// Decimal degrees, or sexagesimal ("hh mm ss.s" / "dd:mm:ss") if needed.
bool ParseAngle(std::string text, bool isRa, double& out) {
    try { size_t used = 0; out = std::stod(text, &used); if (used == text.size()) return true; } catch (...) {}
    for (char& c : text) if (c == ':') c = ' ';
    std::istringstream ss(text);
    std::string p0; double b = 0.0, c = 0.0;
    if (!(ss >> p0 >> b)) return false;
    ss >> c;
    try {
        double sign = (!p0.empty() && p0[0] == '-') ? -1.0 : 1.0;
        double v = std::fabs(std::stod(p0)) + b / 60.0 + c / 3600.0;
        out = isRa ? v * 15.0 : sign * v;
        return true;
    } catch (...) { return false; }
}

// Washington Double Star catalog (USNO; VizieR B/wds/wds): position and most
// recent measured angular separation per pair. Shape matches
// Dataset::LoadDoubleStars (which converts separation x distance -> AU).
json FetchDoubleStars(CURL* curl) {
    auto rows = VizierCsvRows(curl, "SELECT RAJ2000, DEJ2000, sep1, sep2 FROM \"B/wds/wds\"",
                              "the Washington Double Star catalog");
    if (rows.empty()) return json::array();
    const auto& h = rows[0];
    int raI = ColIndex(h, {"RAJ2000", "RA_J2000"}), deI = ColIndex(h, {"DEJ2000", "DE_J2000"});
    int s1I = ColIndex(h, {"SEP1"}), s2I = ColIndex(h, {"SEP2"});
    if (raI < 0 || deI < 0 || (s1I < 0 && s2I < 0)) {
        std::cerr << "WARNING: unexpected WDS CSV header - skipping double stars.\n";
        return json::array();
    }
    json out = json::array();
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (raI >= static_cast<int>(row.size()) || deI >= static_cast<int>(row.size())) continue;
        double ra, dec, sep;
        if (!ParseAngle(row[raI], true, ra) || !ParseAngle(row[deI], false, dec)) continue;
        if (!CellNum(row, s2I, sep) && !CellNum(row, s1I, sep)) continue; // sep2 = most recent
        if (sep <= 0.0) continue;
        out.push_back(json{{"ra_deg", ra}, {"dec_deg", dec}, {"sep_arcsec", sep}});
    }
    return out;
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
        record["mean_anomaly_deg"] = elements.count("ma") ? json(elements["ma"]) : json(nullptr);
        record["epoch_jd"] = data["orbit"].contains("epoch") ? json(JsonNumOrNull(data["orbit"]["epoch"])) : json(nullptr);
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
    bool skipDoubleStars = false;
    double starVmagLimit = kStarCatalogVmagLimit;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out-dir" && i + 1 < argc) outDir = argv[++i];
        else if (arg == "--limit" && i + 1 < argc) limit = std::stoi(argv[++i]);
        else if (arg == "--top-systems" && i + 1 < argc) topN = std::stoi(argv[++i]);
        else if (arg == "--skip-dwarf-planets") skipDwarfPlanets = true;
        else if (arg == "--skip-star-catalog") skipStarCatalog = true;
        else if (arg == "--skip-double-stars") skipDoubleStars = true;
        else if (arg == "--star-vmag-limit" && i + 1 < argc) starVmagLimit = std::stod(argv[++i]);
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

        json starRows = skipStarCatalog ? json::array() : FetchStarCatalog(curl, starVmagLimit);
        if (!starRows.empty()) {
            std::string starPath = outDir + "/star_catalog.json";
            std::ofstream(starPath) << starRows.dump();
            std::cout << "Wrote " << starRows.size() << " real star positions to " << starPath
                      << " (Hipparcos catalog via CDS VizieR TAP - a third, separate data source)\n";
        }

        json doubleRows = skipDoubleStars ? json::array() : FetchDoubleStars(curl);
        if (!doubleRows.empty()) {
            std::string dsPath = outDir + "/double_stars.json";
            std::ofstream(dsPath) << doubleRows.dump();
            std::cout << "Wrote " << doubleRows.size() << " double-star separations to " << dsPath
                      << " (Washington Double Star catalog via CDS VizieR)\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        exitCode = 1;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return exitCode;
}
