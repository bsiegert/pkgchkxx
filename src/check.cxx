#include <thread>

#include "check.hxx"
#include "config_file.hxx"
#include "makevars.hxx"
#include "message.hxx"
#include "nursery.hxx"

namespace fs = std::filesystem;
using namespace pkg_chk;

namespace {
    std::set<pkgpath>
    pkgpaths_to_check(options const& opts, environment const& env) {
        std::set<pkgpath> pkgpaths;
        if (opts.delete_mismatched || opts.update) {
            pkgpaths = env.installed_pkgpaths.get();
        }
        if (opts.add_missing) {
            env.PKGCHK_CONF.get(); // Force the evaluation of PKGCHK_CONF,
                                   // or verbose messages would interleave.
            verbose(opts) << "Append to PKGDIRLIST based on config "
                          << env.PKGCHK_CONF.get() << std::endl;
            config const conf(env.PKGCHK_CONF.get());
            for (auto const& path:
                     conf.apply_tags(
                         env.included_tags.get(), env.excluded_tags.get())) {
                pkgpaths.insert(path);
            }
        }
        return pkgpaths;
    }

    std::set<pkgname>
    latest_pkgnames_from_source(options const& opts, environment const& env, pkgpath const& path) {
        // There are simply no means to enumerate every possible PKGNAME a
        // PKGPATH can provide. So we first extract the default PKGNAME
        // from it, then retrieve other PKGNAMEs according to installed
        // packages. This means:
        //
        // * pkg_chk -a: We'll mark the default PKGNAME as either
        //   MISSING_TODO or OK.
        //
        // * pkg_chk -u: We'll mark installed packages as either
        //   MISMATCH_TODO or OK, and may also mark the default PKGNAME as
        //   MISSING_TODO or OK. MISSING_TODO will be ignored unless -a is
        //   also given so this shouldn't be a problem.
        //
        // * pkg_chk -r: Same as above.
        //
        if (!fs::exists(env.PKGSRCDIR.get() / path / "Makefile")) {
            atomic_warn(
                opts,
                [&](auto& out) {
                    out << "No " << path << "/Makefile - package moved or obsolete?" << std::endl;
                });
            return {};
        }

        auto const default_pkgname =
            extract_pkgmk_var<pkgname>(env.PKGSRCDIR.get() / path, "PKGNAME");
        if (!default_pkgname) {
            fatal(
                opts,
                [&](auto& out) {
                    out << "Unable to extract PKGNAME for " << path << std::endl;
                });
        }

        // We need to search non-default PKGNAMEs only when -u or -r is
        // given, because MISSING_TODO isn't relevant to -a. We can do this
        // unconditionally but that's just a waste of time.
        std::set<pkgname> pkgnames = {
            pkgname(*default_pkgname)
        };
        if (opts.update || opts.delete_mismatched) {
            auto const& pm = env.installed_pkgpaths_with_pkgnames.get();
            if (auto installed_pkgnames = pm.find(path); installed_pkgnames != pm.end()) {
                for (auto const& installed_pkgname: installed_pkgnames->second) {
                    if (installed_pkgname.base != default_pkgname->base) {
                        // We found a non-default PKGBASE but spawning
                        // make(1) takes seriously long. It's really
                        // tempting to cheat by making up a PKGNAME by
                        // combining it with the already known PKGVERSION,
                        // but we can't. This is because previously
                        // supported Python versions (or Ruby, or Lua, or
                        // whatever) may have become unsupported by this
                        // PKGPATH, and we must treat it like a removed
                        // package in that case.
                        auto const alternative_pkgname =
                            extract_pkgmk_var<pkgname>(
                                env.PKGSRCDIR.get() / path,
                                "PKGNAME",
                                {{"PKGNAME_REQD", installed_pkgname.base + "-[0-9]*"}}).value();
                        // If it doesn't support this PKGNAME_REQD, it
                        // reports a PKGNAME whose PKGBASE doesn't match
                        // the requested one.
                        if (alternative_pkgname.base == installed_pkgname.base) {
                            pkgnames.insert(std::move(alternative_pkgname));
                        }
                        else {
                            atomic_warn(
                                opts,
                                [&](auto& out) {
                                    out << path << " had presumably provided a package named like "
                                        << installed_pkgname.base << "-[0-9]* but it no longer does so. "
                                        << "The installed package " << installed_pkgname
                                        << " cannot be updated. Delete it and re-run the command."
                                        << std::endl;
                                });
                            return {};
                        }
                    }
                }
            }
        }

        return pkgnames;
    }

    std::set<pkgname>
    latest_pkgnames_from_binary(environment const& env, pkgpath const& path) {
        throw std::runtime_error("FIXME: -b not implemented yet");
    }

    struct check_result {
        check_result() {}

        check_result(check_result const& res)
            : _MISSING_DONE(res._MISSING_DONE)
            , _MISSING_TODO(res._MISSING_TODO)
            , _MISMATCH_TODO(res._MISMATCH_TODO) {}

        check_result(check_result&& res)
            : _MISSING_DONE(std::move(res._MISSING_DONE))
            , _MISSING_TODO(std::move(res._MISSING_TODO))
            , _MISMATCH_TODO(std::move(res._MISMATCH_TODO)) {}

        std::set<pkgpath> const&
        MISSING_DONE() const {
            return _MISSING_DONE;
        }

        template <typename Pkgpath>
        void
        MISSING_DONE(Pkgpath&& path) {
            lock_t lk(_mtx);
            _MISSING_DONE.insert(path);
        }

        std::map<pkgname, pkgpath> const&
        MISSING_TODO() const {
            return _MISSING_TODO;
        }

        template <typename Pkgname, typename Pkgpath>
        void
        MISSING_TODO(Pkgname&& name, Pkgpath&& path) {
            lock_t lk(_mtx);
            _MISSING_TODO.insert_or_assign(name, path);
        }

        std::set<pkgname> const&
        MISMATCH_TODO() const {
            return _MISMATCH_TODO;
        }

        template <typename Pkgname>
        void
        MISMATCH_TODO(Pkgname&& name) {
            lock_t lk(_mtx);
            _MISMATCH_TODO.insert(name);
        }

    private:
        using mutex_t = std::mutex;
        using lock_t  = std::lock_guard<mutex_t>;

        mutable mutex_t _mtx;

        std::set<pkgpath> _MISSING_DONE;
        std::map<pkgname, pkgpath> _MISSING_TODO;
        std::set<pkgname> _MISMATCH_TODO;
    };

    // This is the slowest part of pkg_chk. For each package we need to
    // extract variables from package Makefiles unless we are using binary
    // packages. Luckily for us each check is independent of each other so
    // we can parallelize them.
    check_result
    check_installed_packages(
        options const& opts,
        environment const& env,
        std::set<pkgpath> const& pkgpaths) {

        check_result res;
        nursery n;
        for (pkgpath const& path: pkgpaths) {
            n.start_soon(
                [&]() {
                    // Find the set of latest PKGNAMEs provided by this
                    // PKGPATH. Most PKGPATHs have just one corresponding
                    // PKGNAME but some (py-*) have more.
                    std::set<pkgname> const latest_pkgnames
                        = opts.build_from_source
                        ? latest_pkgnames_from_source(opts, env, path)
                        : latest_pkgnames_from_binary(env, path);

                    if (latest_pkgnames.empty()) {
                        res.MISSING_DONE(path);
                        return;
                    }

                    auto const& installed_pkgnames = env.installed_pkgnames.get();
                    for (pkgname const& name: latest_pkgnames) {
                        if (auto installed = installed_pkgnames.lower_bound(pkgname(name.base, pkgversion()));
                            installed != installed_pkgnames.end() && installed->base == name.base) {

                            if (installed->version == name.version) {
                                // The latest PKGNAME turned out to be
                                // installed. Good, but that's not enough
                                // if -B is given.
                                if (opts.check_build_version) {
                                    throw std::runtime_error("FIXME: -B not implemented yet");
                                }
                                else {
                                    atomic_verbose(
                                        opts,
                                        [&](auto& out) {
                                            out << path << " - " << name << " OK" << std::endl;
                                        });
                                }
                            }
                            else if (installed->version < name.version) {
                                // We have an older version installed.
                                atomic_msg(
                                    opts,
                                    [&](auto& out) {
                                        out << path << " - " << *installed << " < " << name
                                            << (env.is_binary_available(name) ? " (has binary package)" : "")
                                            << std::endl;
                                    });
                                res.MISMATCH_TODO(*installed);
                            }
                            else {
                                // We have a newer version installed
                                // but how can that happen?
                                if (opts.check_build_version) {
                                    atomic_msg(
                                        opts,
                                        [&](auto& out) {
                                            out << path << " - " << *installed << " > " << name
                                                << (env.is_binary_available(name) ? " (has binary package)" : "")
                                                << std::endl;
                                        });
                                    res.MISMATCH_TODO(*installed);
                                }
                                else {
                                    atomic_msg(
                                        opts,
                                        [&](auto& out) {
                                            out << path << " - " << *installed << " > " << name << " - ignoring"
                                                << (env.is_binary_available(name) ? " (has binary package)" : "")
                                                << std::endl;
                                        });
                                }
                            }
                        }
                        else {
                            atomic_msg(
                                opts,
                                [&](auto& out) {
                                    out << path << " - " << name << " missing"
                                        << (env.is_binary_available(name) ? " (has binary package)" : "")
                                        << std::endl;
                                });
                            res.MISSING_TODO(name, path);
                        }
                    }
                });
        }
        return std::move(res);
    }
}

namespace pkg_chk {
    void
    add_delete_update(options const& opts, environment const& env) {
        std::set<pkgpath> const pkgpaths = pkgpaths_to_check(opts, env);
        if (opts.print_pkgpaths_to_check) {
            for (pkgpath const& path: pkgpaths) {
                std::cout << path << std::endl;
            }
            return;
        }

        check_result const res = check_installed_packages(opts, env, pkgpaths);

        if (!res.MISSING_DONE().empty()) {
            msg(opts) << "Missing:";
            for (pkgpath const& path: res.MISSING_DONE()) {
                msg(opts) << ' ' << path;
            }
            msg(opts) << std::endl;
        }
    }
}
