#include "path.h"
#include <stdexcept>

namespace Velos {
    namespace fs = std::filesystem;

    static bool IsProjectRootCandidate(const fs::path& dir) {
        return fs::exists(dir / "premake5.lua") &&
            fs::exists(dir / "engine") &&
            fs::exists(dir / "runtime");
    }

    void Path::Initialize(const fs::path& argv0)
    {
        if (initialized_) return;

        const fs::path exePath = fs::weakly_canonical(fs::absolute(argv0));
        const fs::path exeDir = exePath.parent_path();

        projectRoot_ = FindProjectRoot(exeDir);

        initialized_ = true;
    }

    const fs::path& Path::ProjectRoot()
    {
        if (!initialized_) {
            throw std::runtime_error("Path::Initialize() must be called before use.");
        }
        return projectRoot_;
    }

    fs::path Path::Resolve(const fs::path& relative)
    {
        if (!initialized_) {
            throw std::runtime_error("Path::Initialize() must be called before use.");
        }
        return projectRoot_ / relative;
    }

    fs::path Path::FindProjectRoot(const fs::path& startDir)
    {
        fs::path current = fs::weakly_canonical(startDir);

        while (!current.empty()) {
            if (IsProjectRootCandidate(current)) {
                return current;
            }

            auto parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }

        throw std::runtime_error("Failed to locate project root.");
    }

}