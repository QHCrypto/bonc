#pragma once

#include <string>
#include <vector>

namespace bonc::sat_modeller {

class TableTemplate {
public:
  enum Entry {
    Unknown,
    Positive,
    Negative,
    NotTaken
  };

private:
  std::vector<std::vector<Entry>> clauses;

public:
  TableTemplate() = default;
  TableTemplate(const TableTemplate&) = default;
  TableTemplate(TableTemplate&&) = default;
  TableTemplate& operator=(const TableTemplate&) = default;
  TableTemplate& operator=(TableTemplate&&) = default;

  void addClause(const std::vector<Entry>& clause) {
    clauses.push_back(clause);
  }

  auto begin(this auto&& self) {
    return self.clauses.begin();
  }
  auto end(this auto&& self) {
    return self.clauses.end();
  }
};

}