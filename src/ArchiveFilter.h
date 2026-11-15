#pragma once
#include <string>
#include <vector>
#include <cctype>

// Same ordered comma-separated includes/excludes as ImGuiTextFilter.
class ArchiveFilter
{
public:
	void Build(const char* query)
	{
		m_terms.clear();
		m_includeCount = 0;
		std::string input(query);
		size_t start = 0;
		do
		{
			const size_t comma = input.find(',', start);
			std::string term = input.substr(start, comma == std::string::npos ? comma : comma - start);
			const size_t first = term.find_first_not_of(" \t");
			if (first != std::string::npos)
			{
				term = term.substr(first, term.find_last_not_of(" \t") - first + 1);
				if (term[0] != '-') ++m_includeCount;
				m_terms.push_back(Lower(term));
			}
			if (comma == std::string::npos) break;
			start = comma + 1;
		} while (start <= input.size());
	}

	bool PassFilter(const char* value) const
	{
		const std::string text = Lower(value ? value : "");
		for (const auto& term : m_terms)
		{
			if (term[0] == '-')
			{
				if (text.find(term.substr(1)) != std::string::npos) return false;
			}
			else if (text.find(term) != std::string::npos) return true;
		}
		return m_includeCount == 0;
	}
private:
	static std::string Lower(std::string value)
	{
		for (char& c : value) if (static_cast<unsigned char>(c) < 128) c = char(std::tolower(static_cast<unsigned char>(c)));
		return value;
	}
	std::vector<std::string> m_terms;
	int m_includeCount = 0;
};
