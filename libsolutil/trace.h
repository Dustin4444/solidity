#pragma once
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

		if (file_.is_open() && !first_event_)
			file_ << ",\n";
		first_event_ = false;

		file_ << "{\"name\":\"" << escapeJson(name)
		      << "\",\"ph\":\"B\",\"ts\":" << static_cast<int64_t>(t * 1000)
		      << ",\"pid\":1,\"tid\":1}";
	}

	void end(const std::string& name)
	{
		if (!enabled_)
			return;

		auto t = since_start();

		if (file_.is_open())
		{
			file_ << ",\n{\"name\":\"" << escapeJson(name)
			      << "\",\"ph\":\"E\",\"ts\":" << static_cast<int64_t>(t * 1000)
			      << ",\"pid\":1,\"tid\":1}";
		}

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

	void finalize()
	{
		if (!enabled_)
			return;

		if (file_.is_open())
		{
			file_ << "\n]}";
			file_.close();
		}

		if (events_.empty())
			return;

		// ===================================================================
		// Compute totals and global span
		// ===================================================================
		double global_start = std::numeric_limits<double>::infinity();
		double global_end = -std::numeric_limits<double>::infinity();

		for (auto& e: events_)
		{
			if (e.end_ms >= 0)
			{
				if (e.start_ms < global_start)
					global_start = e.start_ms;
				if (e.end_ms > global_end)
					global_end = e.end_ms;
			}
		}
		double global_span = (global_end > global_start) ? (global_end - global_start) : 0.0;

		// Inclusive totals per name (children included)
		std::vector<std::pair<std::string, double>> inclusive;
		for (auto& e: events_)
		{
			if (e.end_ms >= 0)
			{
				double dur = e.end_ms - e.start_ms;
				auto it = std::find_if(inclusive.begin(), inclusive.end(),
					[&](auto& p) { return p.first == e.name; });
				if (it != inclusive.end())
					it->second += dur;
				else
					inclusive.emplace_back(e.name, dur);
			}
		}

		// Exclusive totals per event (subtract durations of DIRECT children only)
		std::vector<double> exclusive_per_event(events_.size(), 0.0);
		for (size_t i = 0; i < events_.size(); ++i)
		{
			auto& parent = events_[i];
			if (parent.end_ms < 0)
				continue;
			double dur = parent.end_ms - parent.start_ms;
			double direct_children_sum = 0.0;

			// Find all events nested in parent
			for (size_t j = 0; j < events_.size(); ++j)
			{
				if (i == j)
					continue;
				auto& child = events_[j];
				if (child.end_ms < 0)
					continue;

				// Child must be strictly nested in parent
				if (child.start_ms >= parent.start_ms && child.end_ms <= parent.end_ms)
				{
					// Check if this is a DIRECT child (no intermediate parent)
					bool is_direct = true;
					for (size_t k = 0; k < events_.size(); ++k)
					{
						if (k == i || k == j)
							continue;
						auto& intermediate = events_[k];
						if (intermediate.end_ms < 0)
							continue;

						// If there's an intermediate event between parent and child, skip
						if (child.start_ms >= intermediate.start_ms &&
						    child.end_ms <= intermediate.end_ms &&
						    intermediate.start_ms >= parent.start_ms &&
						    intermediate.end_ms <= parent.end_ms)
						{
							is_direct = false;
							break;
						}
					}

					if (is_direct)
						direct_children_sum += (child.end_ms - child.start_ms);
				}
			}

			double excl = dur - direct_children_sum;
			if (excl < 0)
				excl = 0; // Guard against rounding / overlapping boundaries
			exclusive_per_event[i] = excl;
		}

		// Aggregate exclusive per name
		std::vector<std::pair<std::string, double>> exclusive_by_name;
		for (size_t i = 0; i < events_.size(); ++i)
		{
			if (events_[i].end_ms < 0)
				continue;
			auto it = std::find_if(
				exclusive_by_name.begin(),
				exclusive_by_name.end(),
				[&](auto& p) { return p.first == events_[i].name; });
			if (it != exclusive_by_name.end())
				it->second += exclusive_per_event[i];
			else
				exclusive_by_name.emplace_back(events_[i].name, exclusive_per_event[i]);
		}

		// Sort for display
		auto sort_desc = [](auto& vec)
		{
			std::sort(vec.begin(), vec.end(),
				[](auto& a, auto& b) { return a.second > b.second; });
		};

		sort_desc(inclusive);
		sort_desc(exclusive_by_name);

		// ===================================================================
		// Build hierarchical tree
		// ===================================================================
		struct TreeNode
		{
			size_t event_idx;
			std::vector<TreeNode*> children;
			double exclusive_time;
		};

		std::vector<std::unique_ptr<TreeNode>> all_nodes;
		std::vector<TreeNode*> roots;

		// Create nodes for all events
		for (size_t i = 0; i < events_.size(); ++i)
		{
			if (events_[i].end_ms >= 0)
			{
				auto node = std::make_unique<TreeNode>();
				node->event_idx = i;
				node->exclusive_time = exclusive_per_event[i];
				all_nodes.push_back(std::move(node));
			}
		}

		// Build parent-child relationships
		for (size_t i = 0; i < all_nodes.size(); ++i)
		{
			auto& node = all_nodes[i];
			auto& event = events_[node->event_idx];

			// Find direct parent (closest enclosing event)
			TreeNode* parent = nullptr;
			double parent_span = std::numeric_limits<double>::infinity();

			for (size_t j = 0; j < all_nodes.size(); ++j)
			{
				if (i == j)
					continue;
				auto& potential_parent = all_nodes[j];
				auto& p_event = events_[potential_parent->event_idx];

				// Check if potential_parent encloses this event
				if (event.start_ms >= p_event.start_ms && event.end_ms <= p_event.end_ms)
				{
					double span = p_event.end_ms - p_event.start_ms;
					if (span < parent_span)
					{
						parent = potential_parent.get();
						parent_span = span;
					}
				}
			}

			if (parent)
				parent->children.push_back(node.get());
			else
				roots.push_back(node.get());
		}

		// Sort children by duration (descending) - will be re-sorted after grouping
		for (auto& node : all_nodes)
		{
			std::sort(node->children.begin(), node->children.end(),
				[this](TreeNode* a, TreeNode* b) {
					auto& a_event = events_[a->event_idx];
					auto& b_event = events_[b->event_idx];
					return (a_event.end_ms - a_event.start_ms) > (b_event.end_ms - b_event.start_ms);
				});
		}

		// Sort roots by duration (descending)
		std::sort(roots.begin(), roots.end(),
			[this](TreeNode* a, TreeNode* b) {
				auto& a_event = events_[a->event_idx];
				auto& b_event = events_[b->event_idx];
				return (a_event.end_ms - a_event.start_ms) > (b_event.end_ms - b_event.start_ms);
			});

		// ===================================================================
		// Hierarchical tree printer with grouping
		// ===================================================================
		int nodes_printed = 0;
		const int MAX_DEPTH = 100;

		// Recursive function to print grouped tree
		std::function<void(const std::vector<TreeNode*>&, const std::string&, bool, double, int)> print_grouped_tree;
		print_grouped_tree = [&](const std::vector<TreeNode*>& nodes, const std::string& prefix, bool is_last, double parent_time, int depth) -> void
		{
			if (nodes.empty() || depth > MAX_DEPTH)
				return;

			nodes_printed++;

			// Calculate totals for this group
			std::string name = events_[nodes[0]->event_idx].name;
			double total_duration = 0.0;
			double total_exclusive = 0.0;
			std::vector<TreeNode*> all_children;

			for (auto* node : nodes)
			{
				auto& event = events_[node->event_idx];
				total_duration += (event.end_ms - event.start_ms);
				total_exclusive += node->exclusive_time;
				all_children.insert(all_children.end(), node->children.begin(), node->children.end());
			}

			double pct_total = (global_span > 0) ? (100.0 * total_duration / global_span) : 0.0;
			double pct_parent = (parent_time > 0) ? (100.0 * total_duration / parent_time) : 100.0;

			// Tree characters
			std::string branch = is_last ? "└─ " : "├─ ";
			std::string continuation = is_last ? "   " : "│  ";

			// Print this group
			std::cout << prefix << branch << name;
			if (nodes.size() > 1)
				std::cout << " (×" << nodes.size() << ")";
			std::cout << " " << std::fixed << std::setprecision(2) << total_duration << "ms";
			std::cout << " [" << std::setprecision(1) << pct_total << "% total";
			if (parent_time > 0 && parent_time != global_span)
				std::cout << ", " << std::setprecision(1) << pct_parent << "% of parent";
			std::cout << "]";

			// Show exclusive time if has children
			if (!all_children.empty() && total_exclusive > 0.01)
			{
				double excl_pct = (total_duration > 0) ? (100.0 * total_exclusive / total_duration) : 0.0;
				std::cout << " (self: " << std::setprecision(2) << total_exclusive
				          << "ms, " << std::setprecision(1) << excl_pct << "%)";
			}
			std::cout << "\n";

			// Group all children by name
			std::map<std::string, std::vector<TreeNode*>> children_by_name;
			for (auto* child : all_children)
			{
				std::string child_name = events_[child->event_idx].name;
				children_by_name[child_name].push_back(child);
			}

			// Convert to vector and sort by total duration
			std::vector<std::pair<std::string, std::vector<TreeNode*>>> grouped_children(
				children_by_name.begin(), children_by_name.end());

			std::sort(grouped_children.begin(), grouped_children.end(),
				[this](const auto& a, const auto& b) {
					double a_total = 0.0, b_total = 0.0;
					for (auto* node : a.second)
						a_total += (events_[node->event_idx].end_ms - events_[node->event_idx].start_ms);
					for (auto* node : b.second)
						b_total += (events_[node->event_idx].end_ms - events_[node->event_idx].start_ms);
					return a_total > b_total;
				});

			// Recursively print each group
			for (size_t i = 0; i < grouped_children.size(); ++i)
			{
				bool child_is_last = (i == grouped_children.size() - 1);
				print_grouped_tree(grouped_children[i].second,
					prefix + continuation, child_is_last, total_duration, depth + 1);
			}
		};

		// ===================================================================
		// Print enhanced summaries
		// ===================================================================
		const int NAME_WIDTH = 50;
		const int TIME_WIDTH = 12;
		const int PCT_WIDTH = 8;
		const int BAR_MAX_WIDTH = 40;

		std::cout << "\n";
		std::cout << "╔══════════════════════════════════════════════════════════════════════════════════════════════════════╗\n";
		std::cout << "║  Performance Trace Summary" << std::string(71, ' ') << "║\n";
		std::cout << "║  Total Time: " << std::fixed << std::setprecision(2) << global_span << " ms";
		std::cout << std::string(82 - 15 - std::to_string(static_cast<int>(global_span)).length(), ' ') << "║\n";
		std::cout << "║  Trace File: " << filename_ << std::string(85 - filename_.length(), ' ') << "║\n";
		std::cout << "╚══════════════════════════════════════════════════════════════════════════════════════════════════════╝\n";

		// Print hierarchical view
		std::cout << "\n";
		std::cout << "══ Hierarchical Call Tree ══════════════════════════════════════════════════\n";
		std::cout << "\n";
		for (size_t i = 0; i < roots.size(); ++i)
		{
			bool is_last = (i == roots.size() - 1);
			print_grouped_tree({roots[i]}, "", is_last, global_span, 0);
		}
		std::cout << "\n";
		std::cout << "Total nodes in tree: " << all_nodes.size() << " (displayed: " << nodes_printed << " groups)\n";
		std::cout << "\n";

		std::cout << "\n";
		std::cout << "── Inclusive Time (with children) ──────────────────────────────────────────\n";
		std::cout << "   NOTE: % of wall clock time (can exceed 100% if called multiple times)\n";
		std::cout << std::left << std::setw(NAME_WIDTH) << "Function"
		          << std::right << std::setw(TIME_WIDTH) << "Time (ms)"
		          << std::setw(PCT_WIDTH) << "%"
		          << "  " << "Distribution" << "\n";
		std::cout << std::string(NAME_WIDTH + TIME_WIDTH + PCT_WIDTH + 2 + 12, '-') << "\n";

		int shown = 0;
		for (auto& [name, ms]: inclusive)
		{
			double pct = (global_span > 0) ? (100.0 * ms / global_span) : 0.0;

			// Truncate long names
			std::string displayName = name;
			if (displayName.length() > static_cast<size_t>(NAME_WIDTH - 3))
				displayName = "..." + displayName.substr(displayName.length() - (NAME_WIDTH - 6));

			// Visual bar
			int barWidth = static_cast<int>((pct / 100.0) * BAR_MAX_WIDTH);
			if (barWidth > BAR_MAX_WIDTH)
				barWidth = BAR_MAX_WIDTH;
			std::string bar(barWidth, '#');

			std::cout << std::left << std::setw(NAME_WIDTH) << displayName
			          << std::right << std::setw(TIME_WIDTH) << std::fixed << std::setprecision(2) << ms
			          << std::setw(PCT_WIDTH - 1) << std::setprecision(1) << pct << "%"
			          << "  " << bar << "\n";

			if (++shown >= 30)
			{
				std::cout << "  ... (" << (inclusive.size() - shown) << " more entries)\n";
				break;
			}
		}

		std::cout << "\n";
		std::cout << "── Exclusive Time (without children) ───────────────────────────────────────\n";
		std::cout << "   NOTE: % of wall clock time (this is where time is ACTUALLY spent)\n";
		std::cout << std::left << std::setw(NAME_WIDTH) << "Function"
		          << std::right << std::setw(TIME_WIDTH) << "Time (ms)"
		          << std::setw(PCT_WIDTH) << "%"
		          << "  " << "Distribution" << "\n";
		std::cout << std::string(NAME_WIDTH + TIME_WIDTH + PCT_WIDTH + 2 + 12, '-') << "\n";

		shown = 0;
		double exclusive_total = 0.0;
		for (auto& [name, ms]: exclusive_by_name)
		{
			double pct = (global_span > 0) ? (100.0 * ms / global_span) : 0.0;
			std::string displayName = name;
			if (displayName.length() > static_cast<size_t>(NAME_WIDTH - 3))
				displayName = "..." + displayName.substr(displayName.length() - (NAME_WIDTH - 6));

			int barWidth = static_cast<int>((pct / 100.0) * BAR_MAX_WIDTH);
			if (barWidth > BAR_MAX_WIDTH)
				barWidth = BAR_MAX_WIDTH;
			std::string bar(barWidth, '=');

			std::cout << std::left << std::setw(NAME_WIDTH) << displayName
			          << std::right << std::setw(TIME_WIDTH) << std::fixed << std::setprecision(2) << ms
			          << std::setw(PCT_WIDTH - 1) << std::setprecision(1) << pct << "%"
			          << "  " << bar << "\n";

			exclusive_total += ms;

			if (++shown >= 30)
			{
				// Add remaining items to total
				for (size_t i = shown; i < exclusive_by_name.size(); ++i)
					exclusive_total += exclusive_by_name[i].second;
				std::cout << "  ... (" << (exclusive_by_name.size() - shown) << " more entries)\n";
				break;
			}
		}

		std::cout << std::string(NAME_WIDTH + TIME_WIDTH + PCT_WIDTH + 2, '-') << "\n";
		double exclusive_pct = (global_span > 0) ? (100.0 * exclusive_total / global_span) : 0.0;
		std::cout << std::left << std::setw(NAME_WIDTH) << "TOTAL (exclusive)"
		          << std::right << std::setw(TIME_WIDTH) << std::fixed << std::setprecision(2) << exclusive_total
		          << std::setw(PCT_WIDTH - 1) << std::setprecision(1) << exclusive_pct << "%\n";
		std::cout << std::left << std::setw(NAME_WIDTH) << "Wall clock time"
		          << std::right << std::setw(TIME_WIDTH) << std::fixed << std::setprecision(2) << global_span
		          << std::setw(PCT_WIDTH) << "100.0%\n";

		std::cout << "\n";
		std::cout << "View detailed flamegraph: chrome://tracing or https://ui.perfetto.dev/\n";
		std::cout << "\n";
	}

	bool isEnabled() const { return enabled_; }

private:
	using Clock = std::chrono::high_resolution_clock;

	Tracer()
	{
		// Check if tracing is enabled via environment variable
		const char* enabledEnv = std::getenv("SOLIDITY_TRACE");
		enabled_ = (enabledEnv != nullptr && std::string(enabledEnv) != "0" && std::string(enabledEnv) != "");

		if (!enabled_)
			return;

		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		std::ostringstream name;
		name << "trace_" << std::put_time(std::localtime(&t), "%Y-%m-%dT%H-%M-%S") << ".json";
		filename_ = name.str();

		file_.open(filename_);
		if (file_.is_open())
		{
			file_ << "{\"traceEvents\":[\n";
			start_time_ = Clock::now();
		}
		else
		{
			enabled_ = false;
		}
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

	std::ofstream file_;
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
