#include "ControlDocs.h"

#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace {

// The docs live in assets/control_docs.json so the MCP server can serve them
// without a running DAW. Loaded once, lazily, relative to the working directory
// (the same convention as assets/settings.json).
const json& docsRoot() {
    static const json root = [] {
        std::ifstream f("assets/control_docs.json");
        if (!f.is_open()) {
            std::cerr << "[control] cannot open assets/control_docs.json; help/describe will be empty\n";
            return json::object();
        }
        try {
            return json::parse(f);
        } catch (const std::exception& e) {
            std::cerr << "[control] assets/control_docs.json is not valid JSON: " << e.what() << "\n";
            return json::object();
        }
    }();
    return root;
}

const json& section(const char* key) {
    static const json empty = json::object();
    const json& root = docsRoot();
    auto it = root.find(key);
    return (it != root.end() && it->is_object()) ? *it : empty;
}

} // namespace

const json& controlConcepts() { return section("concepts"); }
const json& actionDocs()      { return section("actions"); }
const json& queryDocs()       { return section("queries"); }
const json& opDocs()          { return section("ops"); }
