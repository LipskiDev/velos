#pragma once

#include <filesystem>
#include <string>

namespace Velos {

    class Path
    {
    public:
        static void Initialize(const std::filesystem::path& argv0);

        static const std::filesystem::path& ProjectRoot();

        static std::filesystem::path Resolve(const std::filesystem::path& relative);

    private:
        static std::filesystem::path FindProjectRoot(const std::filesystem::path& startDir);

    private:
        static inline bool initialized_ = false;
        static inline std::filesystem::path projectRoot_;
    };

}