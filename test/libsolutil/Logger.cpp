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
 * Unit tests for the logging registry.
 */

#include <libsolutil/Logger.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace solidity::log;

namespace solidity::log::test
{

namespace
{

DEFINE_LOGGER(staticLogger, "test.logger.static_registration");

static_assert(isValidCategory("yul"));
static_assert(isValidCategory("yul.ssa.codetransform"));
static_assert(isValidCategory("a0_-.b-9_"));
static_assert(!isValidCategory(""));
static_assert(!isValidCategory("."));
static_assert(!isValidCategory("yul."));
static_assert(!isValidCategory(".yul"));
static_assert(!isValidCategory("yul..ssa"));
static_assert(!isValidCategory("Yul"));
static_assert(!isValidCategory("yul ssa"));
static_assert(!isValidCategory("yul/ssa"));

/// Resets the process-wide singleton before and after a test that configures it. The other tests
/// use registries of their own.
struct SingletonFixture
{
	SingletonFixture() { LoggerRegistry::singleton().reset(); }
	~SingletonFixture() { LoggerRegistry::singleton().reset(); }
};

}

BOOST_AUTO_TEST_SUITE(LoggerTest)

BOOST_AUTO_TEST_CASE(is_valid_category_agrees_with_regex)
{
	std::regex const categoryPattern(R"([a-z0-9_-]+(\.[a-z0-9_-]+)*)");
	auto check = [&](std::string const& _category) {
		bool const matchesRegex = std::regex_match(_category, categoryPattern);
		BOOST_CHECK_MESSAGE(
			isValidCategory(_category) == matchesRegex,
			"isValidCategory(\"" + _category + "\") disagrees with the regex, which " +
			(matchesRegex ? "accepts" : "rejects") + " it"
		);
	};

	for (std::string const category: {
		"yul",
		"yul.ssa.codetransform.shuffler",
		"evmasm.peephole-optimiser",
		"a_b-c.0.9",
		"yul.ssa2",
		"yul..ssa",
		"yul.ssa.",
		".yul.ssa",
		"Yul.ssa",
		"yul.SSA",
		"yul:ssa",
		"yul\tssa",
		"yül",
	})
		check(category);

	// Every string of length up to 4 over the valid characters, the separator and the nearest invalid
	// neighbour of each character range.
	std::string const alphabet = "az09_-.`{/:A ";
	size_t const maxLength = 4;
	std::vector<std::string> current{""};
	for (size_t length = 0; length <= maxLength; ++length)
	{
		std::vector<std::string> next;
		for (std::string const& prefix: current)
		{
			check(prefix);
			if (length < maxLength)
				for (char const c: alphabet)
					next.push_back(prefix + c);
		}
		current = std::move(next);
	}
}

BOOST_AUTO_TEST_CASE(loggers_default_to_off)
{
	LoggerRegistry registry;
	Logger const& logger = registry.get("yul.ssa");
	for (Level level = 0; level <= maxLevel; ++level)
		BOOST_CHECK(!logger.shouldLog(level));
}

BOOST_AUTO_TEST_CASE(higher_level_is_more_verbose)
{
	LoggerRegistry registry;
	Logger const& logger = registry.get("a");

	registry.setLevel("a", 1);
	BOOST_CHECK(logger.shouldLog(1));
	BOOST_CHECK(!logger.shouldLog(2));
	BOOST_CHECK(!logger.shouldLog(3));

	registry.setLevel("a", 2);
	BOOST_CHECK(logger.shouldLog(1));
	BOOST_CHECK(logger.shouldLog(2));
	BOOST_CHECK(!logger.shouldLog(3));

	registry.setLevel("a", 3);
	BOOST_CHECK(logger.shouldLog(1));
	BOOST_CHECK(logger.shouldLog(2));
	BOOST_CHECK(logger.shouldLog(3));

	registry.setLevel("a", 0);
	BOOST_CHECK(!logger.shouldLog(1));
	BOOST_CHECK(!logger.shouldLog(2));
	BOOST_CHECK(!logger.shouldLog(3));
}

BOOST_AUTO_TEST_CASE(level_zero_messages_are_never_logged)
{
	LoggerRegistry registry;
	Logger const& logger = registry.get("a");
	registry.setLevel("a", maxLevel);
	BOOST_CHECK(!logger.shouldLog(0));
}

BOOST_AUTO_TEST_CASE(prefix_matches_whole_components)
{
	BOOST_CHECK(prefixMatches("yul.ssa", "yul.ssa"));
	BOOST_CHECK(prefixMatches("yul.ssa", "yul.ssa.codetransform"));
	BOOST_CHECK(prefixMatches("yul", "yul.ssa.codetransform"));
	BOOST_CHECK(!prefixMatches("yul.ssa", "yul.ssa2"));
	BOOST_CHECK(!prefixMatches("yul.ss", "yul.ssa"));
	BOOST_CHECK(!prefixMatches("yul.ssa.codetransform", "yul.ssa"));
	BOOST_CHECK(!prefixMatches("ssa", "yul.ssa"));
	BOOST_CHECK(prefixMatches("", "yul"));
	BOOST_CHECK(prefixMatches("", "yul.ssa"));
}

BOOST_AUTO_TEST_CASE(longest_matching_prefix_wins)
{
	LoggerRegistry registry;
	// Spec example: the global default is set last and does not override the more specific rules.
	registry.setLevel("yul.ssa", 2);
	registry.setLevel("yul.ssa.codetransform.shuffler", 0);
	registry.setLevel("", 1);

	BOOST_CHECK(registry.get("yul").shouldLog(1));
	BOOST_CHECK(!registry.get("yul").shouldLog(2));

	BOOST_CHECK(registry.get("yul.ssa").shouldLog(2));
	BOOST_CHECK(registry.get("yul.ssa.codetransform").shouldLog(2));
	BOOST_CHECK(!registry.get("yul.ssa.codetransform").shouldLog(3));

	BOOST_CHECK(!registry.get("yul.ssa.codetransform.shuffler").shouldLog(1));
	BOOST_CHECK(!registry.get("yul.ssa.codetransform.shuffler.detail").shouldLog(1));

	// Not a whole-component match of "yul.ssa", so only the global default applies.
	BOOST_CHECK(registry.get("yul.ssa2").shouldLog(1));
	BOOST_CHECK(!registry.get("yul.ssa2").shouldLog(2));
}

BOOST_AUTO_TEST_CASE(result_does_not_depend_on_rule_order)
{
	LoggerRegistry registry;
	std::vector<std::pair<std::string, Level>> rules = {
		{"", 1},
		{"yul", 3},
		{"yul.ssa", 2},
		{"yul.ssa.codetransform.shuffler", 0},
	};
	std::vector<std::string> const categories = {
		"evmasm",
		"yul",
		"yul.opt",
		"yul.ssa",
		"yul.ssa.codetransform",
		"yul.ssa.codetransform.shuffler",
	};

	auto levels = [&]() {
		std::vector<Level> result;
		for (std::string const& category: categories)
		{
			Level level = 0;
			while (level < maxLevel && registry.get(category).shouldLog(static_cast<Level>(level + 1)))
				++level;
			result.push_back(level);
		}
		return result;
	};

	std::sort(rules.begin(), rules.end());
	std::optional<std::vector<Level>> expected;
	do
	{
		registry.reset();
		for (auto const& [prefix, level]: rules)
			registry.setLevel(prefix, level);
		if (!expected)
			expected = levels();
		else
			BOOST_CHECK(levels() == *expected);
	}
	while (std::next_permutation(rules.begin(), rules.end()));

	BOOST_CHECK((*expected == std::vector<Level>{1, 3, 3, 2, 2, 0}));
}

BOOST_AUTO_TEST_CASE(set_level_replaces_rule_for_same_prefix)
{
	LoggerRegistry registry;
	Logger const& logger = registry.get("yul.ssa");
	registry.setLevel("yul", 3);
	registry.setLevel("yul", 1);
	BOOST_CHECK(logger.shouldLog(1));
	BOOST_CHECK(!logger.shouldLog(2));
}

BOOST_AUTO_TEST_CASE(level_change_propagates_to_existing_loggers)
{
	LoggerRegistry registry;
	Logger const& logger = registry.get("yul.ssa.stacklayout");
	BOOST_CHECK(!logger.shouldLog(1));

	registry.setLevel("yul.ssa", 1);
	BOOST_CHECK(logger.shouldLog(1));
}

BOOST_AUTO_TEST_CASE(new_logger_picks_up_existing_rules)
{
	LoggerRegistry registry;
	registry.setLevel("yul.ssa", 2);
	BOOST_CHECK(registry.get("yul.ssa.created_after_rule").shouldLog(2));
}

BOOST_FIXTURE_TEST_CASE(define_logger_registers_during_static_initialisation, SingletonFixture)
{
	LoggerRegistry& registry = LoggerRegistry::singleton();
	std::vector<std::string> const categories = registry.categories();
	BOOST_CHECK(std::is_sorted(categories.begin(), categories.end()));
	BOOST_CHECK(std::find(categories.begin(), categories.end(), "test.logger.static_registration") != categories.end());

	BOOST_CHECK(registry.matchesAnyCategory(""));
	BOOST_CHECK(registry.matchesAnyCategory("test"));
	BOOST_CHECK(registry.matchesAnyCategory("test.logger"));
	BOOST_CHECK(registry.matchesAnyCategory("test.logger.static_registration"));
	BOOST_CHECK(!registry.matchesAnyCategory("test.log"));
	BOOST_CHECK(!registry.matchesAnyCategory("test.logger.static_registration.child"));

	BOOST_CHECK(!staticLogger.shouldLog(1));
	registry.setLevel("test.logger", 1);
	BOOST_CHECK(staticLogger.shouldLog(1));
}

BOOST_AUTO_TEST_CASE(output_has_time_level_and_category_prefix)
{
	LoggerRegistry registry;
	std::ostringstream out;
	registry.setOutput(out);
	registry.setLevel("c", 1);

	Logger const& logger = registry.get("c");
	solLog1(logger, "step={} removed={}", "CSE", 42);
	// "[<seconds, width 8, 3 decimals>s L<level> <category>] <message>\n"
	BOOST_CHECK_MESSAGE(
		std::regex_match(out.str(), std::regex(R"(\[[ 0-9]{3}[0-9]\.[0-9]{3}s L1 c\] step=CSE removed=42\n)")),
		out.str()
	);
}

BOOST_AUTO_TEST_CASE(timestamps_can_be_disabled)
{
	LoggerRegistry registry;
	std::ostringstream out;
	registry.setOutput(out);
	registry.setTimestamps(false);
	registry.setLevel("yul.ssa", 2);

	Logger const& logger = registry.get("yul.ssa");
	solLog1(logger, "step={} removed={}", "CSE", 42);
	solLog2(logger, "no arguments");
	BOOST_CHECK_EQUAL(out.str(), "[L1 yul.ssa] step=CSE removed=42\n[L2 yul.ssa] no arguments\n");

	registry.reset();
	registry.setLevel("yul.ssa", 1);
	out.str("");
	registry.setOutput(out);
	solLog1(logger, "x");
	BOOST_CHECK(out.str().ends_with("s L1 yul.ssa] x\n"));
}

BOOST_AUTO_TEST_CASE(registries_are_independent)
{
	LoggerRegistry first;
	LoggerRegistry second;
	std::ostringstream firstOut;
	std::ostringstream secondOut;
	first.setOutput(firstOut);
	second.setOutput(secondOut);
	first.setTimestamps(false);
	first.setLevel("a", 1);

	solLog1(first.get("a"), "x");
	solLog1(second.get("a"), "x");
	BOOST_CHECK_EQUAL(firstOut.str(), "[L1 a] x\n");
	BOOST_CHECK(secondOut.str().empty());
	BOOST_CHECK(second.categories() == std::vector<std::string>{"a"});
	BOOST_CHECK(!second.matchesAnyCategory("b"));
}

BOOST_AUTO_TEST_CASE(prefix_carries_message_level_not_logger_level)
{
	LoggerRegistry registry;
	std::ostringstream out;
	registry.setOutput(out);
	registry.setLevel("yul.ssa", 3);

	Logger const& logger = registry.get("yul.ssa");
	solLog2(logger, "x");
	solLog3(logger, "y");
	BOOST_CHECK_MESSAGE(
		std::regex_match(out.str(), std::regex(R"(\[[ 0-9.]{8}s L2 yul\.ssa\] x\n\[[ 0-9.]{8}s L3 yul\.ssa\] y\n)")),
		out.str()
	);
}

BOOST_AUTO_TEST_CASE(messages_above_logger_level_are_dropped)
{
	LoggerRegistry registry;
	std::ostringstream out;
	registry.setOutput(out);
	registry.setLevel("c", 1);

	Logger const& logger = registry.get("c");
	solLog2(logger, "dropped");
	solLog3(logger, "dropped");
	BOOST_CHECK(out.str().empty());
}

BOOST_AUTO_TEST_CASE(disabled_logger_does_not_evaluate_arguments)
{
	LoggerRegistry registry;
	std::ostringstream out;
	registry.setOutput(out);

	Logger const& logger = registry.get("d"); // defaults to off

	int evaluations = 0;
	auto sideEffect = [&]() { ++evaluations; return 42; };

	solLog1(logger, "{}", sideEffect());
	BOOST_CHECK_EQUAL(evaluations, 0);
	BOOST_CHECK(out.str().empty());

	registry.setLevel("d", 1);
	solLog1(logger, "{}", sideEffect());
	BOOST_CHECK_EQUAL(evaluations, 1);
	BOOST_CHECK(out.str().ends_with("L1 d] 42\n"));
}

BOOST_AUTO_TEST_CASE(reset_turns_everything_off)
{
	LoggerRegistry registry;
	Logger const& logger = registry.get("c");
	registry.setLevel("", 3);
	BOOST_CHECK(logger.shouldLog(3));

	registry.reset();
	BOOST_CHECK(!logger.shouldLog(1));
	// Rules are gone too, so new loggers start off.
	BOOST_CHECK(!registry.get("c.child").shouldLog(1));
}

BOOST_AUTO_TEST_CASE(parse_level)
{
	BOOST_CHECK(parseLevel("0") == Level{0});
	BOOST_CHECK(parseLevel("3") == Level{3});
	BOOST_CHECK(parseLevel("003") == Level{3});

	// Only ASCII digits are accepted: a non-ASCII character, or a digit from another script (full-width
	// "１", Arabic-Indic "٣"), is rejected.
	for (std::string const level: {
		"", "4", "10", "255", "256", "99999999999999999999", "-1", "+1", "x", "1x", "1.0", " 1", "1 ",
		"😸", "1😸", "１", "٣",
	})
		BOOST_CHECK_MESSAGE(!parseLevel(level), "parseLevel(\"" + level + "\") should have failed");
}

BOOST_AUTO_TEST_CASE(parse_level_specs)
{
	auto const specs = parseLevelSpecs("yul.ssa=2,yul.ssa.codetransform=0,yul=3");
	BOOST_REQUIRE(specs.has_value());
	BOOST_CHECK((*specs == LevelSpecs{{"yul.ssa", 2}, {"yul.ssa.codetransform", 0}, {"yul", 3}}));

	std::vector<std::pair<std::string, std::string>> const expectedMessages = {
		{"", "Invalid log spec ''. Expected <prefix>=<level>."},
		{"yul", "Invalid log spec 'yul'. Expected <prefix>=<level>."},
		{"yul=1,", "Invalid log spec ''. Expected <prefix>=<level>."},
		{"yul=", "Invalid log level '' in log spec 'yul='. Expected a number from 0 to 3."},
		{"yul=4", "Invalid log level '4' in log spec 'yul=4'. Expected a number from 0 to 3."},
		{"yul=1😸", "Invalid log level '1😸' in log spec 'yul=1😸'. Expected a number from 0 to 3."},
		{"yul=1=2", "Invalid log level '1=2' in log spec 'yul=1=2'. Expected a number from 0 to 3."},
		// The global default is not a category.
		{"=3", "Invalid category prefix '' in log spec '=3'."},
		{"yul.=1", "Invalid category prefix 'yul.' in log spec 'yul.=1'."},
		{"Yul=1", "Invalid category prefix 'Yul' in log spec 'Yul=1'."},
		{" yul=1", "Invalid category prefix ' yul' in log spec ' yul=1'."},
		{"😸=1", "Invalid category prefix '😸' in log spec '😸=1'."},
		// The prefix is validated before the level.
		{"😸=4", "Invalid category prefix '😸' in log spec '😸=4'."},
		{"yul=1,yul.ssa=2,yul=1", "Category prefix 'yul' is given more than once."},
	};
	for (auto const& [input, expectedMessage]: expectedMessages)
	{
		auto const result = parseLevelSpecs(input);
		BOOST_REQUIRE(!result.has_value());
		BOOST_CHECK_EQUAL(result.error(), expectedMessage);
	}
}

BOOST_AUTO_TEST_SUITE_END()

}
