/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

/**
 * Runtime switchable, hierarchical logging for the compiler developers. Not a user-facing facility:
 * user-relevant errors and warnings go through ErrorReporter.
 *
 * Loggers are named hierarchically with '.' separators, e.g. "yul.ssa.stacklayout". Each logger is
 * registered with LoggerRegistry by DEFINE_LOGGER during static initialisation, so DEFINE_LOGGER must
 * be used at namespace scope. Every logger starts at level 0 (off) until the CLI configures it.
 * Use the solLog1/solLog2/solLog3 macros at the call sites.
 */
#pragma once

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace solidity::log
{

/// Log verbosity level: Higher values means more verbose
/// 1: major events and summaries
/// 2: per-function detail and optimizer decisions
/// 3: debug dumps
using Level = std::uint8_t;
constexpr Level maxLevel = 3;

/// @returns true if @a _category matches `[a-z0-9_-]+(\.[a-z0-9_-]+)*`.
constexpr bool isValidCategory(std::string_view _category) noexcept
{
	bool componentEmpty = true;
	for (char const c: _category)
		if (c == '.')
		{
			if (componentEmpty)
				return false;
			componentEmpty = true;
		}
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
			componentEmpty = false;
		else
			return false;
	return !componentEmpty;
}

/// @returns true if @a _prefix matches @a _category on whole '.'-separated components.
/// e.g.: yul.ssa matches both `yul.ssa` and `yul.ssa.codetransform`
/// The empty prefix matches every category.
bool prefixMatches(std::string_view _prefix, std::string_view _category) noexcept;

/// @returns the level denoted by @a _level, or nullopt if it is not a decimal number from 0 to maxLevel.
std::optional<Level> parseLevel(std::string_view _level) noexcept;

/// (prefix, level) pairs: the level to apply to every category matching the prefix.
using LevelSpecs = std::vector<std::pair<std::string, Level>>;

/// Parses a comma-separated list of `<prefix>=<level>` specs, e.g. `yul.ssa=2,yul.ssa.codetransform=0`.
/// Every prefix must be a valid category name and may be given only once.
/// @returns the specs in the order given, or an error message.
std::expected<LevelSpecs, std::string> parseLevelSpecs(std::string_view _specs);

class LoggerRegistry;

class Logger
{
public:
	Logger(Logger const&) = delete;
	Logger(Logger&&) = delete;
	Logger& operator=(Logger const&) = delete;
	Logger& operator=(Logger&&) = delete;

	bool shouldLog(Level _level) const noexcept { return _level != 0 && _level <= m_level; }

	/// Logs without checking the level. Only meant to be called through the solLog macros.
	template<typename... Args>
	void logUnchecked(Level _level, fmt::format_string<Args...> _format, Args&&... _args) const
	{
		// Type-erased, so that no formatting code is instantiated at the call site.
		vlog(_level, _format.get(), fmt::make_format_args(_args...));
	}

private:
	friend class LoggerRegistry;

	Logger(LoggerRegistry& _registry, std::string _category, Level _level):
		m_registry(_registry),
		m_category(std::move(_category)),
		m_level(_level)
	{}

	void vlog(Level _level, fmt::string_view _format, fmt::format_args _args) const;

	LoggerRegistry& m_registry;
	std::string const m_category;
	/// Effective level, kept up to date by the registry.
	Level m_level;
};

/// Registry of loggers. The compiler uses the process-wide singleton().
class LoggerRegistry
{
public:
	LoggerRegistry();
	LoggerRegistry(LoggerRegistry const&) = delete;
	LoggerRegistry& operator=(LoggerRegistry const&) = delete;

	static LoggerRegistry& singleton();

	/// @returns the logger for @a _category, creating and registering it first if it doesn't exist yet.
	Logger const& get(std::string_view _category);

	/// Adds the rule (@a _prefix, @a _level), replacing any existing rule for the same prefix, then sets
	/// every logger to the level of its longest matching rule prefix, or 0 if none matches.
	void setLevel(std::string_view _prefix, Level _level);

	/// @returns true if @a _prefix matches at least one registered category.
	bool matchesAnyCategory(std::string_view _prefix) const;

	/// @returns sorted registered categories
	std::vector<std::string> categories() const;

	/// Directs log output to @a _stream, which must outlive its use by the registry.
	void setOutput(std::ostream& _stream);

	/// Sets whether the line prefix carries the time since startup. Enabled by default.
	void setTimestamps(bool _enabled);

	void reset();

private:
	Level computeEffectiveLevel(std::string_view _category) const;

	/// Writes a line prefixed with "[<seconds since startup>s L<level> <category>]", or with
	/// "[L<level> <category>]" if timestamps are disabled, and flushes the output.
	void write(Level _level, std::string_view _category, fmt::string_view _format, fmt::format_args _args);

	friend class Logger;

	std::chrono::steady_clock::time_point const m_startTime;
	std::map<std::string, Level, std::less<>> m_rules;
	std::unordered_map<std::string, std::unique_ptr<Logger>> m_loggers;
	std::ostream* m_output;
	bool m_timestamps = true;
};

}

/// Defines a logger reference bound once to @a dottedCategory, which must be a string literal matching
/// `[a-z0-9_-]+(\.[a-z0-9_-]+)*`.
/// Must be used at namespace scope, so that the category is registered during static initialisation, before the CLI is parsed.
/// The unnamed namespace makes a use anywhere else a compilation error.
#define DEFINE_LOGGER(variableName, dottedCategory) \
	namespace \
	{ \
	::solidity::log::Logger const& variableName = \
		::solidity::log::LoggerRegistry::singleton().get(dottedCategory); \
	} \
	static_assert( \
		::solidity::log::isValidCategory(dottedCategory), \
		"Invalid logger category \"" dottedCategory "\": must match [a-z0-9_-]+(\\.[a-z0-9_-]+)*" \
	)

/// Arguments are only evaluated if the logger is enabled at @a level.
#define solLog(logger, level, ...) \
	do \
	{ \
		static_assert( \
			1 <= (level) && (level) <= ::solidity::log::maxLevel, \
			"Log level must be a constant between 1 and solidity::log::maxLevel" \
		); \
		::solidity::log::Logger const& solLogger_ = (logger); \
		if (solLogger_.shouldLog(level)) [[unlikely]] \
			solLogger_.logUnchecked((level), __VA_ARGS__); \
	} while (0)

#define solLog1(logger, ...) solLog((logger), 1, __VA_ARGS__)
#define solLog2(logger, ...) solLog((logger), 2, __VA_ARGS__)
#define solLog3(logger, ...) solLog((logger), 3, __VA_ARGS__)
