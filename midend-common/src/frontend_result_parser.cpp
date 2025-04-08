#include "frontend_result_parser.h"

namespace bonc {

FrontendResultParser::FrontendResultParser(std::istream& json_content) {
  value = nlohmann::json::parse(json_content);
}

std::vector<OutputInfo> FrontendResultParser::parseAll() {
  // Parse inputs
  for (const auto& input : value.at("inputs")) {
    auto name = input.at("name").get<std::string>();
    auto size = input.at("size").get<std::size_t>();
    read_targets["input:" + name] =
        std::make_shared<ReadTarget>(ReadTarget::Input, name, size);
  }

  // Parse components.sboxes
  for (const auto& sbox : value.at("components").at("sboxes")) {
    auto name = sbox.at("name").get<std::string>();
    auto value = sbox.at("value").get<std::vector<std::uint64_t>>();
    auto input_width = sbox.at("input_width").get<std::uint64_t>();
    auto output_width = sbox.at("output_width").get<std::uint64_t>();
    lookup_tables[name] =
        std::make_shared<LookupTable>(name, input_width, output_width, value);
  }

  // Parse iterations
  for (const auto& iteration : value.at("iterations")) {
    auto name = iteration.at("name").get<std::string>();
    auto size = iteration.at("size").get<std::size_t>();
    auto target = std::make_shared<ReadTarget>(ReadTarget::State, name, size);

    if (iteration.contains("update_expressions")) {
      for (const auto& expr : iteration.at("update_expressions")) {
        target->update_expressions.push_back(BitExpr::fromJSON(*this, expr));
      }
    }

    read_targets["state:" + name] = target;
  }
  std::vector<OutputInfo> outputs;

  for (const auto& output : value.at("outputs")) {
    OutputInfo info;
    info.name = output.at("name").get<std::string>();
    info.size = output.at("size").get<unsigned>();
    for (const auto& expr : output.at("expressions")) {
      info.expressions.push_back(BitExpr::fromJSON(*this, expr));
    }
    outputs.push_back(info);
  }

  return outputs;
}

std::shared_ptr<BitExpr> BitExpr::fromJSON(const FrontendResultParser& parser,
                                           const nlohmann::json& j) {
  const std::string type = j.at("type").get<std::string>();

  if (type == "constant") {
    // Parse constant_expression
    auto value = j.at("value").get<int>();
    return std::make_shared<ConstantBitExpr>(value);
  } else if (type == "read") {
    // Parse read_expression
    auto target_name = j.at("target_name").get<std::string>();
    auto offset = j.at("offset").get<int>();
    auto target = parser.getReadTarget(target_name);
    return ReadBitExpr::create(target, offset);
  } else if (type == "lookup") {
    // Parse lookup_expression
    auto table_name = j.at("table_name").get<std::string>();
    auto table = parser.getLookupTable(table_name);
    std::vector<std::shared_ptr<BitExpr>> inputs;
    for (const auto& input : j.at("inputs")) {
      inputs.push_back(fromJSON(parser, input));
    }
    unsigned output_offset = j.at("output_offset").get<unsigned>();
    return LookupBitExpr::create(table, std::move(inputs), output_offset);
  } else if (type == "unary") {
    // Parse unary_expression
    auto op = j.at("operator").get<std::string>();
    if (op == "not") {
      auto operand = fromJSON(parser, j.at("operand"));
      return NotBitExpr::create(operand);
    }
  } else if (type == "binary") {
    // Parse binary_expression
    auto op = j.at("operator").get<std::string>();
    auto left = fromJSON(parser, j.at("left"));
    auto right = fromJSON(parser, j.at("right"));
    if (op == "and") {
      return std::make_shared<BinaryBitExpr>(BinaryBitExpr::And, left, right);
    } else if (op == "or") {
      return std::make_shared<BinaryBitExpr>(BinaryBitExpr::Or, left, right);
    } else if (op == "xor") {
      return std::make_shared<BinaryBitExpr>(BinaryBitExpr::Xor, left, right);
    }
  }

  throw std::invalid_argument("Unknown BitExpr type: " + type);
}

void ReadBitExpr::print(std::ostream& os) const {
  os << target->getName() << "[" << offset << "]";
}

void LookupBitExpr::print(std::ostream& os) const {
  os << table->getName() << "(";
  for (size_t i = 0; i < inputs.size(); ++i) {
    inputs[i]->print(os);
    if (i < inputs.size() - 1) {
      os << ",";
    }
  }
  os << ")[" << output_offset << "]";
}

void NotBitExpr::print(std::ostream& os) const {
  os << "!";
  expr->print(os);
}

void BinaryBitExpr::print(std::ostream& os) const {
  os << "(";
  left->print(os);
  switch (kind) {
    case And: os << " & "; break;
    case Or: os << " | "; break;
    case Xor: os << " ^ "; break;
    default: assert(false && "Invalid binary operator");
  }
  right->print(os);
  os << ")";
}

std::shared_ptr<ReadTarget> FrontendResultParser::getReadTarget(
    const std::string& name) const {
  return read_targets.at(name);
}

std::shared_ptr<LookupTable> FrontendResultParser::getLookupTable(
    const std::string& name) const {
  return lookup_tables.at(name);
}

ANFPolynomial<std::shared_ptr<ReadBitExpr>> bitExprToANF(
    std::shared_ptr<BitExpr> expr, int read_depth) {
  switch (expr->getKind()) {
    case BitExpr::Constant:
      return ANFPolynomial<std::shared_ptr<ReadBitExpr>>(
          std::static_pointer_cast<ConstantBitExpr>(expr)->getValue());
    case BitExpr::Read: {
      if (read_depth < 0) {
        return ANFPolynomial<std::shared_ptr<ReadBitExpr>>::fromVariable(
            std::static_pointer_cast<ReadBitExpr>(expr));
      }
      while (expr->getKind() == BitExpr::Read) {
        auto read_expr = std::static_pointer_cast<ReadBitExpr>(expr);
        auto read_target = read_expr->getTarget();
        auto offset = read_expr->getOffset();
        auto kind = read_target->getKind();
        auto name = read_target->getName();
        if (kind != ReadTarget::State) {
          break;
        }
        expr = read_target->update_expressions.at(offset);
      }
      auto result = bitExprToANF(expr, read_depth - 1);
      return result;
    }
    case BitExpr::Lookup:
      throw std::runtime_error("Lookup expr not implemented");
    case BitExpr::Not:
      return !bitExprToANF(
          std::static_pointer_cast<NotBitExpr>(expr)->getExpr(), read_depth);
    case BitExpr::And: {
      auto binary_expr = std::static_pointer_cast<BinaryBitExpr>(expr);
      return bitExprToANF(binary_expr->getLeft(), read_depth)
           * bitExprToANF(binary_expr->getRight(), read_depth);
    }
    case BitExpr::Xor: {
      auto binary_expr = std::static_pointer_cast<BinaryBitExpr>(expr);
      return bitExprToANF(binary_expr->getLeft(), read_depth)
           + bitExprToANF(binary_expr->getRight(), read_depth);
    }
    case BitExpr::Or: {
      auto binary_expr = std::static_pointer_cast<BinaryBitExpr>(expr);
      return !(!bitExprToANF(binary_expr->getLeft(), read_depth)
               * !bitExprToANF(binary_expr->getRight(), read_depth));
    }
    default: throw std::runtime_error("Unknown BitExpr kind");
  }
}

}  // namespace bonc