#ifndef DIAGNOSTIC_REDACTION_H
#define DIAGNOSTIC_REDACTION_H

#include <string>

namespace diagnostic_redaction {

std::string RedactUrlForDiagnostics(const std::string& url);

} // namespace diagnostic_redaction

#endif // DIAGNOSTIC_REDACTION_H
