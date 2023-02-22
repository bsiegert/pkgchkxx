#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <system_error>
#include <vector>

#include "bzip2stream.hxx"
#include "gzipstream.hxx"
#include "harness.hxx"
#include "message.hxx"
#include "string_algo.hxx"
#include "summary.hxx"
#include "wwwstream.hxx"
#include "xargs_fold.hxx"

using namespace pkg_chk;
using namespace std::literals;
namespace fs = std::filesystem;

namespace {
    std::string const shell = "/bin/sh";

    std::vector<std::string> const SUMMARY_FILES = {
        "pkg_summary.bz2",
        "pkg_summary.gz",
        "pkg_summary.txt"
    };

    summary
    read_summary(std::istream& in) {
        summary sum;
        std::vector<pkgpattern> DEPENDS;
        std::optional<pkgname> PKGNAME;
        std::optional<pkgpath> PKGPATH;
        for (std::string line; std::getline(in, line); ) {
            if (line.empty()) {
                if (PKGNAME && PKGPATH) {
                    DEPENDS.shrink_to_fit();
                    sum.emplace(
                        PKGNAME.value(),
                        pkgvars {
                            std::move(DEPENDS),
                            PKGNAME.value(),
                            PKGPATH.value()
                        });
                }
                DEPENDS.clear();
                PKGNAME.reset();
                PKGPATH.reset();
            }
            else if (auto const equal = line.find('='); equal != std::string::npos) {
                auto const view     = static_cast<std::string_view>(line);
                auto const variable = view.substr(0, equal);
                auto const value    = equal + 1 < view.size() ? view.substr(equal + 1) : ""sv;

                if (variable == "DEPENDS") {
                    DEPENDS.emplace_back(value);
                }
                else if (variable == "PKGNAME") {
                    PKGNAME.emplace(value);
                }
                else if (variable == "PKGPATH") {
                    PKGPATH.emplace(value);
                }
            }
        }
        return sum;
    }

    template <typename Function>
    auto
    with_uncompress_filter(
        std::filesystem::path const& summary_file,
        std::istream&& maybe_compressed,
        Function&& f) {

        auto const&& ext = summary_file.extension();
        if (ext == ".bz2") {
            bunzip2istream in(maybe_compressed);
            return f(in);
        }
        else if (ext == ".gz") {
            gunzipistream in(maybe_compressed);
            return f(in);
        }
        else {
            return f(maybe_compressed);
        }
    }

    summary
    read_local_summary(
        options const& opts,
        std::filesystem::path const& PACKAGES,
        std::filesystem::path const& PKG_INFO,
        std::string const& PKG_SUFX) {

        // Lazily find the newest binary package, lazily because if no
        // summary files exist this information won't be used.
        auto const newest_bin_pkg = std::async(
            std::launch::deferred,
            [&PACKAGES]() {
                fs::file_time_type t;
                for (auto const& ent:
                         fs::directory_iterator(
                             PACKAGES,
                             fs::directory_options::follow_directory_symlink)) {
                    if (ent.last_write_time() > t) {
                        t = ent.last_write_time();
                    }
                }
                return t;
            }).share();

        for (auto const& summary_file: SUMMARY_FILES) {
            auto const path = PACKAGES / summary_file;
            std::error_code ec;
            auto const summary_last_mod = fs::last_write_time(path, ec);
            if (ec) {
                continue;
            }
            // Is there any binary package that is newer than the summary
            // file? Ignore the summary if so.
            else if (summary_last_mod < newest_bin_pkg.get()) {
                msg(opts) << "** Ignoring " << path
                          << " as there are newer packages in " << PACKAGES << std::endl;
                continue;
            }
            else {
                verbose(opts) << "Using summary file: " << path << std::endl;
                std::fstream local_file(path, std::ios_base::in);
                if (!local_file) {
                    throw std::system_error(
                        errno, std::generic_category(), "Failed to open " + path.string());
                }

                return with_uncompress_filter(
                    path, std::move(local_file),
                    [](auto&& in) {
                        return read_summary(in);
                    });
            }
        }

        verbose(opts) << "No valid summaries exist. Scanning "
                      << PACKAGES << " ..." << std::endl;
        return xargs_fold({
                shell,
                "-c", "exec " + PKG_INFO.string() + " -X \"$@\"",
                shell // This will be $0 of the shell, and the rest of argv
                      // will be constructed by xargs.
            },
            [&](auto&& args) {
                for (auto const& ent:
                         fs::directory_iterator(
                             PACKAGES,
                             fs::directory_options::follow_directory_symlink)) {
                    if (ends_with(ent.path().filename().string(), PKG_SUFX)) {
                        args.push_back(ent.path());
                    }
                }
            },
            read_summary);
    }

    summary
    read_remote_summary(options const& opts, std::filesystem::path const& PACKAGES) {
        for (auto const& summary_file: SUMMARY_FILES) {
            try {
                auto const path = PACKAGES / summary_file;
                return with_uncompress_filter(
                    path,
                    wwwistream(path),
                    [](auto&& in) {
                        return read_summary(in);
                    });
            }
            catch (remote_file_unavailable const&) {
                continue;
            }
        }

        fatal(opts, [&](auto&& out) {
                        out << "No summary files are available: "
                            << PACKAGES.string() << std::endl;
                    });
    }
}

namespace pkg_chk {
    summary::summary(
        options const& opts,
        std::filesystem::path const& PACKAGES,
        std::filesystem::path const& PKG_INFO,
        std::string const& PKG_SUFX) {

        if (PACKAGES.string().find("://") != std::string::npos) {
            *this = read_remote_summary(opts, PACKAGES);
        }
        else {
            *this = read_local_summary(opts, PACKAGES, PKG_INFO, PKG_SUFX);
        }
    }

    pkgmap::pkgmap(summary const& all_packages) {
        for (auto const& pair: all_packages) {
            (*this)[pair.second.PKGPATH][pair.second.PKGNAME.base]
                .emplace(pair.first, pair.second);
        }
    }
}
