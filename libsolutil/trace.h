#pragma once
#include "nlohmann/json.hpp"


#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace trace
{

// ============================================================================
// String Utilities for extracting class/method names from __PRETTY_FUNCTION__
// ============================================================================

/// Extract just the class name from __PRETTY_FUNCTION__
/// "void solidity::yul::ExpressionInliner::run(...)" -> "ExpressionInliner"
std::string constexpr extractClassName(const char* prettyFunction)
{
	std::string func(prettyFunction);

	// Find the last '(' to remove parameters
	size_t paren = func.find('(');
	if (paren != std::string::npos)
		func = func.substr(0, paren);

	// Find the last '::' (before the method name)
	size_t lastColon = func.rfind("::");
	if (lastColon == std::string::npos)
		return func; // Free function, no class

	// Now find the second-to-last '::' to get the class name start
	size_t classStart = func.rfind("::", lastColon - 1);
	if (classStart == std::string::npos)
		classStart = 0; // No namespace before class
	else
		classStart += 2; // Skip the '::'

	// Extract from class start to the last '::'
	return func.substr(classStart, lastColon - classStart);
}

/// Extract class::method from __PRETTY_FUNCTION__
/// "void solidity::yul::ExpressionInliner::run(...)" -> "ExpressionInliner::run"
inline std::string extractClassMethod(const char* prettyFunction)
{
	std::string func(prettyFunction);

	// Remove parameters
	size_t paren = func.find('(');
	if (paren != std::string::npos)
		func = func.substr(0, paren);

	// Remove return type (everything before the last space before '::')
	size_t lastColon = func.find("::");
	if (lastColon != std::string::npos)
	{
		size_t spaceBeforeClass = func.rfind(' ', lastColon);
		if (spaceBeforeClass != std::string::npos)
			func = func.substr(spaceBeforeClass + 1);
	}

	// Keep only the last two components (Class::method)
	size_t lastColonPos = func.rfind("::");
	if (lastColonPos != std::string::npos)
	{
		size_t prevColonPos = func.rfind("::", lastColonPos - 1);
		if (prevColonPos != std::string::npos)
			func = func.substr(prevColonPos + 2);
	}

	return func;
}

// ============================================================================
// Main Tracer Class
// ============================================================================

class Tracer
{
public:
	struct Record
	{
		std::string name;
		double start_ms;
		double end_ms;
	};

	struct Node {
		Record rec;
		std::vector<Node> children;
	};

	static Tracer& instance()
	{
		static Tracer inst;
		return inst;
	}

	void begin(const std::string& name)
	{
		if (!enabled_)
			return;

		auto t = since_start();
		events_.push_back({name, t, -1});

		first_event_ = false;
	}

	void end(const std::string& name)
	{
		if (!enabled_)
			return;

		auto t = since_start();

		// Fill end time for the last matching open event
		for (auto it = events_.rbegin(); it != events_.rend(); ++it)
		{
			if (it->name == name && it->end_ms < 0)
			{
				it->end_ms = t;
				break;
			}
		}
	}

	static nlohmann::json to_json(const Node& n) {
		nlohmann::json j;
		j["name"] = n.rec.name;
		j["start_ms"] = n.rec.start_ms;
		j["end_ms"] = n.rec.end_ms;
		j["duration"] = n.rec.end_ms - n.rec.start_ms;
		j["children"] = nlohmann::json::array();
		for (auto const& c : n.children)
			j["children"].push_back(to_json(c));
		return j;
	}

	static std::vector<Node> build_tree(std::vector<Record> records) {
		std::ranges::sort(records,
				  [](auto const& a, auto const& b) { return a.start_ms < b.start_ms; });

		std::vector<Node> roots;
		std::vector<Node*> stack;

		for (auto& r : records) {
			Node node{r, {}};

			while (!stack.empty() && stack.back()->rec.end_ms <= r.start_ms)
				stack.pop_back();

			if (stack.empty()) {
				roots.push_back(std::move(node));
				stack.push_back(&roots.back());
			} else {
				stack.back()->children.push_back(std::move(node));
				stack.push_back(&stack.back()->children.back());
			}
		}

		return roots;
	}

	static void to_json(const Node& n,
				 nlohmann::json& jtree,
				 nlohmann::json& jflat,
				 const std::string* parent = nullptr,
				 int depth = 0)
	{
		// Create the tree node
		nlohmann::json jnode;
		jnode["name"] = n.rec.name;
		jnode["start_ms"] = n.rec.start_ms;
		jnode["end_ms"] = n.rec.end_ms;
		jnode["children"] = nlohmann::json::array();

		// Add flat entry
		nlohmann::json flat_entry;
		flat_entry["name"] = n.rec.name;
		flat_entry["start_ms"] = n.rec.start_ms;
		flat_entry["end_ms"] = n.rec.end_ms;
		flat_entry["duration"] = n.rec.end_ms - n.rec.start_ms;
		flat_entry["depth"] = depth;
		flat_entry["parent"] = parent ? *parent : "";
		jflat.push_back(flat_entry);

		// Recurse for children
		for (auto const& c : n.children)
			to_json(c, jnode["children"], jflat, &n.rec.name, depth + 1);

		jtree.push_back(std::move(jnode));
	}

	static nlohmann::json make_combined_json(const Node& root) {
		nlohmann::json tree{};
		nlohmann::json flat{};

		to_json(root, tree, flat);

		nlohmann::json result;
		result["tree"] = std::move(tree);
		result["flat"] = std::move(flat);
		return result;
	}

	void finalize()
	{
		if (!enabled_)
			return;

		if (events_.empty())
			return;

		// ===================================================================
		// Compute totals and global span
		// ===================================================================
		double global_start = std::numeric_limits<double>::infinity();
		double global_end = -std::numeric_limits<double>::infinity();

		std::map<std::string, double> durations;
		double total_wo_main = 0;
		for (auto& e: events_)
		{
			if (e.end_ms >= 0)
			{
				if (e.start_ms < global_start)
					global_start = e.start_ms;
				if (e.end_ms > global_end)
					global_end = e.end_ms;
			}
			auto const duration_ms = e.end_ms - e.start_ms;
			durations[e.name] += duration_ms;
			if (e.name != "int main")
				total_wo_main += duration_ms;
		}
		for (auto const& [lbl, dur]: durations)
			std::cout << "PERF: " << lbl << " " << dur << std::endl;

		auto const roots = build_tree(events_);
		if (roots.size() != 1)
			throw std::logic_error("that shouldn't happen");
		auto const j = make_combined_json(roots[0]);
		std::cout << "HIERARCHY: " << j.dump() << std::endl;
	}

	bool isEnabled() const { return enabled_; }

private:
	using Clock = std::chrono::high_resolution_clock;

	Tracer()
	{
		// Check if tracing is enabled via environment variable
		enabled_ = true;

		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		start_time_ = Clock::now();
	}

	~Tracer()
	{
		finalize();
	}

	double since_start() const
	{
		auto now = Clock::now();
		return std::chrono::duration<double, std::milli>(now - start_time_).count();
	}

	std::string escapeJson(const std::string& s) const
	{
		std::string result;
		result.reserve(s.length());
		for (char c : s)
		{
			switch (c)
			{
			case '"':  result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\b': result += "\\b"; break;
			case '\f': result += "\\f"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:
				result += c;
			}
		}
		return result;
	}

	Clock::time_point start_time_;
	std::vector<Record> events_;
	std::string filename_;
	bool enabled_ = true;
	bool first_event_ = true;
};

// ============================================================================
// RAII Scope Guard
// ============================================================================

class Scope
{
public:
	explicit Scope(std::string n): name_(std::move(n))
	{
		Tracer::instance().begin(name_);
	}

	~Scope()
	{
		Tracer::instance().end(name_);
	}

private:
	std::string name_;
};

} // namespace trace

// ============================================================================
// Convenience Macros
// ============================================================================

/// Trace using only the class name (e.g., "ExpressionInliner")
#define TRACE_SCOPE_CLASS() trace::Scope _(trace::extractClassName(__PRETTY_FUNCTION__))

/// Trace using class::method (e.g., "ExpressionInliner::run")
#define TRACE_SCOPE_METHOD() trace::Scope _(trace::extractClassMethod(__PRETTY_FUNCTION__))

/// Trace using the full pretty function signature
#define TRACE_SCOPE_FULL() trace::Scope _(__PRETTY_FUNCTION__)

/// Trace with a custom name
#define TRACE_SCOPE(name) trace::Scope _(name)
