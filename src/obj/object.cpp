/**
 * object.cpp — オブジェクトハンドラの登録・キャッシュ・外部コマンド呼び出し
 */

#include "typeset/obj/object.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "typeset/obj/svg_import.hpp"
#include "typeset/text/utf.hpp"

#ifdef _WIN32
#include <process.h>
#define TS_POPEN _popen
#define TS_PCLOSE _pclose
#else
#include <unistd.h>
#define TS_POPEN popen
#define TS_PCLOSE pclose
#endif

namespace typeset::obj {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

std::string fmtNum(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v));
    return buf;
}

std::string tempPath(const char* stem) {
    static int counter = 0;
    const char* dir = std::getenv("TMP");
    if (!dir) dir = std::getenv("TEMP");
    if (!dir) dir = std::getenv("TMPDIR");
    std::string base = dir ? dir : ".";
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    std::ostringstream os;
    os << base << "typeset_" << stem << "_" <<
#ifdef _WIN32
        _getpid()
#else
        getpid()
#endif
       << "_" << (++counter) << ".json";
    return os.str();
}

} // namespace

std::string requestToJson(const ObjectRequest& req) {
    std::string j = "{";
    j += "\"handler\":\"" + jsonEscape(req.handler) + "\",";
    j += "\"source\":\"" + jsonEscape(text::utf16ToUtf8(req.source)) + "\",";
    j += "\"params\":{";
    bool first = true;
    for (const auto& kv : req.params) {
        if (!first) j += ",";
        first = false;
        j += "\"" + jsonEscape(kv.first) + "\":\"" + jsonEscape(kv.second) + "\"";
    }
    j += "},";
    j += "\"maxInline\":" + fmtNum(req.maxInline) + ",";
    j += "\"maxBlock\":" + fmtNum(req.maxBlock) + ",";
    j += "\"fontSize\":" + fmtNum(req.fontSize) + ",";
    j += std::string("\"inline\":") + (req.inlineContext ? "true" : "false") + ",";
    j += std::string("\"vertical\":") + (isVertical(req.writingMode) ? "true" : "false");
    j += "}";
    return j;
}

ObjectResult makeResult(Size size, Pt baseline, std::vector<dl::Item> items) {
    ObjectResult r;
    r.size = size;
    r.baseline = baseline;
    r.hasBaseline = baseline > 0.0f;
    r.items = std::move(items);
    return r;
}

//------------------------------------------------------------------------------

struct ObjectRegistry::Impl {
    std::map<std::string, ObjectHandler> handlers;
    std::unordered_map<std::string, std::shared_ptr<const ObjectResult>> cache;

    static std::string cacheKey(const ObjectRequest& req) {
        std::string k = req.handler + '\x1f' + text::utf16ToUtf8(req.source) + '\x1f';
        for (const auto& kv : req.params) k += kv.first + '=' + kv.second + '\x1e';
        k += '\x1f' + fmtNum(req.fontSize) + '\x1f' + fmtNum(req.maxInline) + '\x1f' +
             (req.inlineContext ? "i" : "b") + (isVertical(req.writingMode) ? "v" : "h");
        return k;
    }
};

ObjectRegistry::ObjectRegistry() : impl_(std::make_unique<Impl>()) {}
ObjectRegistry::~ObjectRegistry() = default;

void ObjectRegistry::add(const std::string& name, ObjectHandler handler) {
    impl_->handlers[name] = std::move(handler);
}

void ObjectRegistry::addCommand(const std::string& name, const std::string& commandLine,
                                const std::string& workDir) {
    add(name, [commandLine, workDir](const ObjectRequest& req) -> ObjectResult {
        ObjectResult res;
        const std::string in = tempPath("req");
        {
            std::ofstream f(in, std::ios::binary);
            if (!f) { res.error = "cannot write request file: " + in; return res; }
            f << requestToJson(req);
        }
        std::string cmd = commandLine + " \"" + in + "\"";
        if (!workDir.empty()) {
#ifdef _WIN32
            cmd = "cd /d \"" + workDir + "\" && " + cmd;
#else
            cmd = "cd \"" + workDir + "\" && " + cmd;
#endif
        }
        std::string output;
        FILE* pipe = TS_POPEN(cmd.c_str(), "r");
        if (!pipe) {
            std::remove(in.c_str());
            res.error = "cannot run: " + commandLine;
            return res;
        }
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) output.append(buf, n);
        const int rc = TS_PCLOSE(pipe);
        std::remove(in.c_str());
        if (rc != 0) {
            res.error = "command failed (" + std::to_string(rc) + "): " + commandLine;
            if (!output.empty()) res.error += "\n" + output.substr(0, 500);
            return res;
        }
        SvgImportOptions so;
        so.fontSize = req.fontSize;
        if (!importSvg(output, so, res)) {
            if (res.error.empty()) res.error = "svg import failed";
            res.error = commandLine + ": " + res.error;
        }
        return res;
    });
}

bool ObjectRegistry::has(const std::string& name) const {
    return impl_->handlers.count(name) > 0;
}

std::shared_ptr<const ObjectResult> ObjectRegistry::render(const ObjectRequest& req) {
    const std::string key = Impl::cacheKey(req);
    auto it = impl_->cache.find(key);
    if (it != impl_->cache.end()) return it->second;

    auto res = std::make_shared<ObjectResult>();
    auto h = impl_->handlers.find(req.handler);
    if (h == impl_->handlers.end()) {
        res->error = "no handler: " + req.handler;
    } else {
        try {
            *res = h->second(req);
        } catch (const std::exception& e) {
            res->error = std::string("handler threw: ") + e.what();
        }
    }
    if (!res->error.empty()) errors_.push_back(req.handler + ": " + res->error);
    impl_->cache.emplace(key, res);
    return res;
}

void ObjectRegistry::clearCache() { impl_->cache.clear(); }
size_t ObjectRegistry::cacheSize() const { return impl_->cache.size(); }

} // namespace typeset::obj
