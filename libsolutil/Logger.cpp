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

#include <libsolutil/Logger.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <charconv>
#include <iostream>

using namespace solidity::log;

bool solidity::log::prefixMatches(std::string_view _prefix, std::string_view _category) noexcept
{
	if (_prefix.empty())
		return true;
	return
		_category.starts_with(_prefix) &&
		(_category.size() == _prefix.size() || _category[_prefix.size()] == '.');
}

std::optional<Level> solidity::log::parseLevel(std::string_view _level) noexcept
{
	char const* const end = _level.data() + _level.size();
	unsigned level = 0;
	auto const [parsedEnd, error] = std::from_chars(_level.data(), end, level);
	if (error != std::errc{} || parsedEnd != end || level > maxLevel)
		return std::nullopt;
	return static_cast<Level>(level);
}

std::expected<LevelSpecs, std::string> solidity::log::parseLevelSpecs(std::string_view _specs)
{
	LevelSpecs result;
	std::vector<std::string> specs;
	for (std::string const& spec: boost::split(specs, _specs, boost::is_any_of(",")))
	{
		auto const separator = spec.find('=');
		if (separator == std::string::npos)
			return std::unexpected(fmt::format("Invalid log spec '{}'. Expected <prefix>=<level>.", spec));

		std::string const prefix = spec.substr(0, separator);
		if (!isValidCategory(prefix))
			return std::unexpected(fmt::format("Invalid category prefix '{}' in log spec '{}'.", prefix, spec));

		std::string const levelString = spec.substr(separator + 1);
		std::optional<Level> const level = parseLevel(levelString);
		if (!level)
			return std::unexpected(fmt::format(
				"Invalid log level '{}' in log spec '{}'. Expected a number from 0 to {}.",
				levelString,
				spec,
				unsigned{maxLevel}
			));

		if (std::ranges::any_of(result, [&](auto const& _spec) { return _spec.first == prefix; }))
			return std::unexpected(fmt::format("Category prefix '{}' is given more than once.", prefix));

		result.emplace_back(prefix, *level);
	}
	return result;
}

LoggerRegistry& LoggerRegistry::singleton()
{
	static LoggerRegistry registry;
	return registry;
}

LoggerRegistry::LoggerRegistry():
	m_startTime(std::chrono::steady_clock::now()),
	m_output(&std::cerr)
{
}

Logger const& LoggerRegistry::get(std::string_view _category)
{
	std::string category{_category};
	if (
		auto const logger = m_loggers.find(category);
		logger != m_loggers.end()
	)
		return *logger->second;

	// Not make_unique: the constructor is only accessible to the registry.
	std::unique_ptr<Logger> newLogger(new Logger(*this, category, computeEffectiveLevel(category)));
	return *m_loggers.emplace(category, std::move(newLogger)).first->second;
}

void LoggerRegistry::setLevel(std::string_view _prefix, Level _level)
{
	m_rules.insert_or_assign(std::string(_prefix), _level);

	for (auto& [category, logger]: m_loggers)
		logger->m_level = computeEffectiveLevel(category);
}

bool LoggerRegistry::matchesAnyCategory(std::string_view _prefix) const
{
	return std::any_of(m_loggers.begin(), m_loggers.end(), [&](auto const& _logger) {
		return prefixMatches(_prefix, _logger.first);
	});
}

std::vector<std::string> LoggerRegistry::categories() const
{
	std::vector<std::string> result;
	result.reserve(m_loggers.size());
	for (auto const& [category, _]: m_loggers)
		result.push_back(category);
	std::sort(result.begin(), result.end());
	return result;
}

void LoggerRegistry::setOutput(std::ostream& _stream)
{
	m_output = &_stream;
}

void LoggerRegistry::setTimestamps(bool _enabled)
{
	m_timestamps = _enabled;
}

void LoggerRegistry::reset()
{
	m_rules.clear();
	for (auto& [category, logger]: m_loggers)
		logger->m_level = 0;
	setOutput(std::cerr);
	setTimestamps(true);
}

Level LoggerRegistry::computeEffectiveLevel(std::string_view _category) const
{
	// Look up the category itself, then each shorter whole-component prefix, then the empty prefix.
	std::string_view prefix = _category;
	while (true)
	{
		if (
			auto const rule = m_rules.find(prefix);
			rule != m_rules.end()
		)
			return rule->second;
		if (prefix.empty())
			return 0;
		auto const dot = prefix.rfind('.');
		prefix = prefix.substr(0, dot == std::string_view::npos ? 0 : dot);
	}
}

void LoggerRegistry::write(Level _level, std::string_view _category, fmt::string_view _format, fmt::format_args _args)
{
	fmt::memory_buffer line;
	fmt::appender out(line);
	if (m_timestamps)
	{
		std::chrono::duration<double> const elapsed = std::chrono::steady_clock::now() - m_startTime;
		fmt::format_to(out, "[{:8.3f}s L{} {}] ", elapsed.count(), unsigned{_level}, _category);
	}
	else
		fmt::format_to(out, "[L{} {}] ", unsigned{_level}, _category);
	fmt::vformat_to(out, _format, _args);
	line.push_back('\n');

	m_output->write(line.data(), static_cast<std::streamsize>(line.size()));
	// The last lines before a crash are the ones that matter.
	m_output->flush();
}

void Logger::vlog(Level _level, fmt::string_view _format, fmt::format_args _args) const
{
	m_registry.write(_level, m_category, _format, _args);
}
