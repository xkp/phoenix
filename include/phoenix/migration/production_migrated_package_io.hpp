#pragma once

#include "phoenix/migration/production_migrated_package.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class PackageIoDiagnosticCode {
    cannot_open_file,
    invalid_format,
};

struct PackageIoDiagnostic {
    PackageIoDiagnosticCode code;
    std::string message;
};

struct PackageReadResult {
    MigratedProjectPackage package;
    std::vector<PackageIoDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class MigratedProjectPackageWriter {
public:
    [[nodiscard]] std::vector<PackageIoDiagnostic> write(
        const MigratedProjectPackage& package,
        const std::filesystem::path& path) const;
};

class MigratedProjectPackageReader {
public:
    [[nodiscard]] PackageReadResult read(const std::filesystem::path& path) const;
};

[[nodiscard]] std::string to_string(PackageIoDiagnosticCode code);

} // namespace phoenix::migration
