#pragma once

#include "../core/text_json.h"

namespace codex_dashboard {

struct ReleaseVersion {
    std::array<std::wstring, 3> core;
    std::vector<std::wstring> prerelease;
    std::wstring text;
};

bool IsNumericVersionPart(const std::wstring& part) {
    return !part.empty() &&
           std::all_of(part.begin(), part.end(), [](wchar_t ch) {
               return ch >= L'0' && ch <= L'9';
           });
}

bool SplitVersionParts(const std::wstring& value,
                       std::vector<std::wstring>& parts) {
    size_t start = 0;
    for (;;) {
        size_t end = value.find(L'.', start);
        std::wstring part = value.substr(start, end == std::wstring::npos
                                                   ? end
                                                   : end - start);
        if (part.empty() ||
            !std::all_of(part.begin(), part.end(), [](wchar_t ch) {
                return (ch >= L'0' && ch <= L'9') ||
                       (ch >= L'a' && ch <= L'z') ||
                       (ch >= L'A' && ch <= L'Z') || ch == L'-';
            })) {
            return false;
        }
        parts.push_back(part);
        if (end == std::wstring::npos) {
            return true;
        }
        start = end + 1;
    }
}

bool ParseReleaseVersion(const std::wstring& value, ReleaseVersion& version) {
    ReleaseVersion parsed;
    parsed.text = TrimString(value);
    if (!parsed.text.empty() && parsed.text.front() == L'v') {
        parsed.text.erase(0, 1);
    }
    std::wstring core = parsed.text;
    size_t buildStart = core.find(L'+');
    if (buildStart != std::wstring::npos) {
        std::vector<std::wstring> build;
        if (!SplitVersionParts(core.substr(buildStart + 1), build)) {
            return false;
        }
        core.resize(buildStart);
    }
    size_t prereleaseStart = core.find(L'-');
    if (prereleaseStart != std::wstring::npos) {
        if (!SplitVersionParts(core.substr(prereleaseStart + 1), parsed.prerelease)) {
            return false;
        }
        for (const auto& part : parsed.prerelease) {
            if (IsNumericVersionPart(part) && part.size() > 1 && part.front() == L'0') {
                return false;
            }
        }
        core.resize(prereleaseStart);
    }
    std::vector<std::wstring> coreParts;
    if (!SplitVersionParts(core, coreParts) || coreParts.size() != parsed.core.size()) {
        return false;
    }
    for (size_t i = 0; i < coreParts.size(); i++) {
        if (!IsNumericVersionPart(coreParts[i]) ||
            (coreParts[i].size() > 1 && coreParts[i].front() == L'0')) {
            return false;
        }
        parsed.core[i] = coreParts[i];
    }
    version = std::move(parsed);
    return true;
}

int CompareNumericVersionParts(const std::wstring& left,
                               const std::wstring& right) {
    if (left.size() != right.size()) {
        return left.size() > right.size() ? 1 : -1;
    }
    return left == right ? 0 : left > right ? 1 : -1;
}

int CompareReleaseVersions(const ReleaseVersion& left, const ReleaseVersion& right) {
    for (size_t i = 0; i < left.core.size(); i++) {
        int comparison = CompareNumericVersionParts(left.core[i], right.core[i]);
        if (comparison != 0) {
            return comparison;
        }
    }
    if (left.prerelease.empty() || right.prerelease.empty()) {
        return left.prerelease.empty() == right.prerelease.empty()
                   ? 0
                   : left.prerelease.empty() ? 1 : -1;
    }
    size_t commonParts = std::min(left.prerelease.size(), right.prerelease.size());
    for (size_t i = 0; i < commonParts; i++) {
        const auto& leftPart = left.prerelease[i];
        const auto& rightPart = right.prerelease[i];
        if (leftPart == rightPart) {
            continue;
        }
        bool leftNumeric = IsNumericVersionPart(leftPart);
        bool rightNumeric = IsNumericVersionPart(rightPart);
        if (leftNumeric && rightNumeric) {
            return CompareNumericVersionParts(leftPart, rightPart);
        }
        if (leftNumeric != rightNumeric) {
            return leftNumeric ? -1 : 1;
        }
        return leftPart > rightPart ? 1 : -1;
    }
    return left.prerelease.size() == right.prerelease.size()
               ? 0
               : left.prerelease.size() > right.prerelease.size() ? 1 : -1;
}

bool IsNewerReleaseVersion(const std::wstring& candidate,
                          const std::wstring& current) {
    ReleaseVersion candidateVersion, currentVersion;
    return ParseReleaseVersion(candidate, candidateVersion) &&
           ParseReleaseVersion(current, currentVersion) &&
           CompareReleaseVersions(candidateVersion, currentVersion) > 0;
}

}  // namespace codex_dashboard
