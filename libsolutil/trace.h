#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace trace
{

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
		auto t = since_start();
		events_.push_back({name, t, -1});
		file_ << "{\"name\":\"" << name << "\",\"ph\":\"B\",\"ts\":" << t << ",\"pid\":1,\"tid\":1},";
	}

	void end(const std::string& name)
	{
		auto t = since_start();
		file_ << "{\"name\":\"" << name << "\",\"ph\":\"E\",\"ts\":" << t << ",\"pid\":1,\"tid\":1},";
		// fill end time for the last matching open event
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
		file_ << "{}]}";
		file_.close();

		// Summarize
		// replace the summarize/finalize part in your Tracer::finalize() with this:

		// --- compute totals and global span ---
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

		// inclusive totals per name (children included)
		std::vector<std::pair<std::string, double>> inclusive;
		for (auto& e: events_)
		{
			if (e.end_ms >= 0)
			{
				double dur = e.end_ms - e.start_ms;
				auto it = std::find_if(inclusive.begin(), inclusive.end(), [&](auto& p) { return p.first == e.name; });
				if (it != inclusive.end())
					it->second += dur;
				else
					inclusive.emplace_back(e.name, dur);
			}
		}

		// exclusive totals per event (subtract durations of strictly nested events)
		std::vector<double> exclusive_per_event(events_.size(), 0.0);
		for (size_t i = 0; i < events_.size(); ++i)
		{
			auto& parent = events_[i];
			if (parent.end_ms < 0)
				continue;
			double dur = parent.end_ms - parent.start_ms;
			double nested_sum = 0.0;
			for (size_t j = 0; j < events_.size(); ++j)
			{
				if (i == j)
					continue;
				auto& child = events_[j];
				if (child.end_ms < 0)
					continue;
				// strictly nested: child wholly inside parent
				if (child.start_ms >= parent.start_ms && child.end_ms <= parent.end_ms)
				{
					nested_sum += (child.end_ms - child.start_ms);
				}
			}
			double excl = dur - nested_sum;
			if (excl < 0)
				excl = 0; // guard against rounding / overlapping boundaries
			exclusive_per_event[i] = excl;
		}

		// aggregate exclusive per name
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

		// sort for display
		auto sort_desc = [](auto& vec)
		{ std::sort(vec.begin(), vec.end(), [](auto& a, auto& b) { return a.second > b.second; }); };

		sort_desc(inclusive);
		sort_desc(exclusive_by_name);

		// --- print summaries ---
		std::cout << "\n=== Timing Summary (relative to overall span: " << std::fixed << std::setprecision(2)
				  << global_span << " ms) ===\n";

		std::cout << "\n-- Inclusive (name totals, children INCLUDED) --\n";
		for (auto& [name, ms]: inclusive)
		{
			double pct = (global_span > 0) ? (100.0 * ms / global_span) : 0.0;
			std::cout << std::setw(12) << name << "  " << std::setw(8) << std::fixed << std::setprecision(2) << ms
					  << " ms  (" << std::setw(5) << std::setprecision(1) << pct << "%)\n";
		}

		std::cout << "\n-- Exclusive (time spent only in this scope, children EXCLUDED) --\n";
		double excl_total = 0;
		for (auto& [_, ms]: exclusive_by_name)
			excl_total += ms;
		for (auto& [name, ms]: exclusive_by_name)
		{
			double pct = (global_span > 0) ? (100.0 * ms / global_span) : 0.0;
			std::cout << std::setw(12) << name << "  " << std::setw(8) << std::fixed << std::setprecision(2) << ms
					  << " ms  (" << std::setw(5) << std::setprecision(1) << pct << "%)\n";
		}

		std::cout << std::setw(12) << "total" << "  " << std::setw(8) << std::fixed << std::setprecision(2)
				  << global_span << " ms  (100%)\n";
	}

	void event(const std::string& name, const std::string& phase, double ts_ms)
	{
		file_ << "{\"name\":\"" << name << "\",\"ph\":\"" << phase << "\",\"ts\":" << ts_ms << ",\"pid\":1,\"tid\":1},";
	}

private:
	using Clock = std::chrono::high_resolution_clock;

	Tracer()
	{
		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		std::ostringstream name;
		name << "trace_" << std::put_time(std::localtime(&t), "%Y-%m-%dT%H-%M-%S") << ".json";
		file_.open(name.str());
		file_ << R"({"traceEvents":[)";
		start_time_ = Clock::now();
	}

	~Tracer() { finalize(); }

	double since_start() const
	{
		auto now = Clock::now();
		return std::chrono::duration<double, std::milli>(now - start_time_).count();
	}

	std::ofstream file_;
	Clock::time_point start_time_;
	std::vector<Record> events_;
};

class Scope
{
public:
	explicit Scope(std::string n): name_(std::move(n)) { Tracer::instance().begin(name_); }
	~Scope() { Tracer::instance().end(name_); }

private:
	std::string name_;
};

} // namespace trace
