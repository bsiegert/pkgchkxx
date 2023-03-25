#pragma once

#include <cassert>
#include <iterator>
#include <optional>

#include <pkgxx/ordered.hxx>

namespace pkgxx {
    /** An iterator that iterates through words in a string. */
    struct word_iterator: equality_comparable<word_iterator> {
        using iterator_category = std::forward_iterator_tag; ///< The category of the iterator.
        using value_type        = std::string_view;          ///< The value of the iterator.
        using pointer           = value_type*;               ///< The pointer type of the iterator.
        using reference         = value_type&;               ///< The reference type of the iterator.

        /// Construct an invalid word iterator that acts as \c .end()
        word_iterator()
            : _source(nullptr) {}

        /// Construct a word iterator pointing at the first word in a given
        /// string. This class acts as a reference wrapper to the string.
        /// That is, the constructor does not copy it.
        word_iterator(std::string_view const& source,
                      std::string      const& seps   = " \t")
            : _source(&source)
            , _seps(seps)
            , _pos(_source->find_first_not_of(_seps)) {

            update_current();
        }

        /// Iterator equality.
        bool
        operator== (word_iterator const& other) const noexcept {
            if (_current.has_value()) {
                return _source == other._source && other._current.has_value();
            }
            else {
                return !other._current.has_value();
            }
        }

        /// Iterator dereference.
        reference
        operator* () {
            return _current.value();
        }

        /// Iterator dereference.
        pointer
        operator-> () {
            _current.value();
            return _current.operator->();
        }

        /// Iterator incrementation.
        word_iterator&
        operator++ () {
            _pos += _current.value().size();
            _pos  = _source->find_first_not_of(_seps, _pos + 1);
            update_current();
            return *this;
        }

        /// Iterator incrementation.
        word_iterator
        operator++ (int) {
            word_iterator it = *this;
            ++(*this);
            return it;
        }

    private:
        void
        update_current() {
            if (_pos != std::string_view::npos) {
                auto const space = _source->find_first_of(_seps, _pos + 1);
                if (space != std::string_view::npos) {
                    _current = _source->substr(_pos, space - _pos);
                }
                else {
                    _current = _source->substr(_pos);
                }
            }
            else {
                _current.reset();
            }
        }

        std::string_view const* _source;
        std::string _seps;
        std::string_view::size_type _pos;
        std::optional<value_type> _current;
    };

    /// Split a string into words like shells do.
    struct words {
        /// The const iterator that iterates through words in a string.
        using const_iterator = word_iterator const;

        /// Construct an instance of \ref words. This class acts as a
        /// reference wrapper to the string given to the constructor. That
        /// is, the constructor does not copy the given string.
        words(std::string_view const& str,
              std::string      const& seps = " \t")
            : _str(str)
            , _seps(seps) {}

        /// Return an iterator to the first word.
        const_iterator
        begin() const noexcept {
            return word_iterator(_str, _seps);
        }

        /// Return an iterator past the last word.
        const_iterator
        end() const noexcept {
            return word_iterator();
        }

    private:
        std::string_view const& _str;
        std::string const& _seps;
    };

    /// Remove whitespaces at the beginning and the end of a string.
    inline std::string_view
    trim(std::string_view const& str,
         std::string_view const& seps = " \t") {

        auto const begin = str.find_first_not_of(seps);
        if (begin != std::string_view::npos) {
            auto const end = str.find_last_not_of(seps);
            assert(end != std::string_view::npos);
            return str.substr(begin, end - begin + 1);
        }
        else {
            return std::string_view();
        }
    }

    /** std::string::starts_with() is a C++20 thing. We can't use it
     * atm. */
    inline bool
    starts_with(std::string_view const& str, std::string_view const& prefix) {
        return str.substr(0, prefix.size()) == prefix;
    }

    /** std::string::ends_with() is a C++20 thing. We can't use it
     * atm. */
    inline bool
    ends_with(std::string_view const& str, std::string_view const& suffix) {
        return
            str.size() >= suffix.size() &&
            str.substr(str.size() - suffix.size()) == suffix;
    }
}