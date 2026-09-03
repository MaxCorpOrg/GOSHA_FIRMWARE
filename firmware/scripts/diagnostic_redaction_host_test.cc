#include "diagnostic_redaction.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

void RequireDoesNotContain(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        Require(haystack.find(needle) == std::string::npos, needle.c_str());
    }
}

} // namespace

int main() {
    const std::string sensitive_url =
        "HTTPS://robot-user:robot-secret@example.invalid:18876/firmware/private/binary.bin?token=top-secret&sig=abc#frag";
    const std::string redacted = diagnostic_redaction::RedactUrlForDiagnostics(sensitive_url);

    Require(redacted.find("scheme=https") != std::string::npos, "scheme was not normalized");
    Require(redacted.find("len=") == std::string::npos, "full URL length must not be exposed");
    Require(redacted.find("path") != std::string::npos, "path flag is missing");
    Require(redacted.find("query") != std::string::npos, "query flag is missing");
    Require(redacted.find("fragment") != std::string::npos, "fragment flag is missing");
    Require(redacted.find("userinfo") != std::string::npos, "userinfo flag is missing");
    RequireDoesNotContain(redacted, {
        "robot-user",
        "robot-secret",
        "example.invalid",
        "18876",
        "firmware",
        "private",
        "binary.bin",
        "token",
        "top-secret",
        "sig=abc",
        "#frag",
    });

    const std::string malformed = "firmware/private/binary.bin?token=top-secret";
    const std::string malformed_redacted = diagnostic_redaction::RedactUrlForDiagnostics(malformed);
    Require(malformed_redacted.find("scheme=url") != std::string::npos, "missing fallback scheme");
    Require(malformed_redacted.find("len=") == std::string::npos, "malformed URL length must not be exposed");
    RequireDoesNotContain(malformed_redacted, {
        "firmware",
        "private",
        "binary.bin",
        "token",
        "top-secret",
    });

    std::cout << "diagnostic redaction host test passed" << std::endl;
    return 0;
}
