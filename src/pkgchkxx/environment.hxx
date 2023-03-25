#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <set>
#include <string>

#include <pkgxx/summary.hxx>

#include "options.hxx"
#include "tag.hxx"

namespace pkg_chk {
    /** Values from the environment such as various Makefiles. Most of such
     * values are very expensive to retrieve so they are lazily
     * evaluated.
     *
     * Objects of this class MUST NOT be shared by threads. Each thread
     * must have its own copy.
     */
    struct environment {
        environment(pkg_chk::options const& opts);

        bool
        is_binary_available(pkgxx::pkgname const& name) const {
            return bin_pkg_summary.get().count(name) > 0;
        }

        std::optional<std::filesystem::path>
        binary_package_file_of(pkgxx::pkgname const& name) const;

        std::shared_future<std::filesystem::path> PKG_PATH;
        std::shared_future<std::string>           MACHINE_ARCH;
        std::shared_future<std::filesystem::path> MAKECONF;
        std::shared_future<std::string>           OPSYS;
        std::shared_future<std::string>           OS_VERSION;
        std::shared_future<std::filesystem::path> PKGSRCDIR;
        std::shared_future<std::filesystem::path> PACKAGES;
        std::shared_future<std::string>           PKG_ADD;
        std::shared_future<std::string>           PKG_ADMIN;
        std::shared_future<std::filesystem::path> PKG_DBDIR;
        std::shared_future<std::string>           PKG_DELETE;
        std::shared_future<std::string>           PKG_INFO;
        std::shared_future<std::string>           PKG_SUFX;
        std::shared_future<std::filesystem::path> PKGCHK_CONF;
        std::shared_future<tagset>                PKGCHK_NOTAGS;
        std::shared_future<tagset>                PKGCHK_TAGS;
        std::shared_future<std::filesystem::path> PKGCHK_UPDATE_CONF;
        std::shared_future<std::string>           SU_CMD;

        std::shared_future<pkgxx::summary> bin_pkg_summary;
        std::shared_future<pkgxx::pkgmap>  bin_pkg_map;

        std::shared_future<std::set<pkgxx::pkgname>> installed_pkgnames; // Fastest to compute.
        std::shared_future<std::set<pkgxx::pkgpath>> installed_pkgpaths; // Moderately slow.
        std::shared_future<pkgxx::summary>        installed_pkg_summary; // Slowest to compute.
        std::shared_future<std::set<pkgxx::pkgbase>> installed_pkgbases; // Derived from installed_pkgnames.
        std::shared_future<
            std::map<
                pkgxx::pkgpath,
                std::set<pkgxx::pkgname>>> installed_pkgpaths_with_pkgnames; // Derived from installed_pkg_summary.

        std::shared_future<tagset>  included_tags;
        std::shared_future<tagset>  excluded_tags;
    };
}