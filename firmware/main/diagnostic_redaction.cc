#include "diagnostic_redaction.h"

#include <algorithm>
#include <cctype>

namespace diagnostic_redaction {
namespace {

bool IsSafeScheme(const std::string& scheme) {
    return !scheme.empty() && std::all_of(scheme.begin(), scheme.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '+' || ch == '-' || ch == '.';
    });
}

std::string NormalizeScheme(std::string scheme) {
    if (!IsSafeScheme(scheme)) {
        return "url";
    }
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return scheme;
}

void AppendFlag(std::string& flags, const char* flag) {
    if (!flags.empty()) {
        flags += ",";
    }
    flags += flag;
}

} // namespace

std::string RedactUrlForDiagnostics(const std::string& url) {
    const auto scheme_end = url.find("://");
    const bool has_scheme = scheme_end != std::string::npos;
    const std::string scheme = has_scheme ? NormalizeScheme(url.substr(0, scheme_end)) : "url";
    const auto inspect_start = has_scheme ? scheme_end + 3 : 0;

    bool has_userinfo = false;
    bool has_path = false;
    if (has_scheme) {
        auto authority_end = url.find_first_of("/?#", inspect_start);
        if (authority_end == std::string::npos) {
            authority_end = url.size();
        }
        const std::string authority = url.substr(inspect_start, authority_end - inspect_start);
        has_userinfo = authority.find('@') != std::string::npos;
        has_path = authority_end < url.size() && url[authority_end] == '/';
    } else {
        has_path = url.find('/') != std::string::npos;
    }

    const bool has_query = url.find('?', inspect_start) != std::string::npos;
    const bool has_fragment = url.find('#', inspect_start) != std::string::npos;

    std::string flags;
    if (has_path) {
        AppendFlag(flags, "path");
    }
    if (has_query) {
        AppendFlag(flags, "query");
    }
    if (has_fragment) {
        AppendFlag(flags, "fragment");
    }
    if (has_userinfo) {
        AppendFlag(flags, "userinfo");
    }
    if (flags.empty()) {
        flags = "none";
    }

    return "scheme=" + scheme + " flags=" + flags;
}

} // namespace diagnostic_redaction
