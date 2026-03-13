#include "Evalvisitor.h"
#include "Python3Parser.h"
#include "Python3Lexer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

using namespace antlr4;

// Helper function to extract variable name from a test context
std::string extractVarNameFromTest(Python3Parser::TestContext* test) {
    if (!test) return "";
    auto or_test = test->or_test();
    if (!or_test || or_test->and_test().empty()) return "";
    auto and_test = or_test->and_test(0);
    if (and_test->not_test().empty()) return "";
    auto not_test = and_test->not_test(0);
    if (!not_test->comparison()) return "";
    auto comparison = not_test->comparison();
    if (comparison->arith_expr().empty()) return "";
    auto arith = comparison->arith_expr(0);
    if (arith->term().empty()) return "";
    auto term = arith->term(0);
    if (term->factor().empty()) return "";
    auto factor = term->factor(0);
    if (!factor->atom_expr()) return "";
    auto atom_expr = factor->atom_expr();
    if (!atom_expr->atom()) return "";
    auto atom = atom_expr->atom();
    if (!atom->NAME()) return "";
    return atom->NAME()->getText();
}

// Helper function to parse string literals
std::string parseStringLiteral(const std::string& s) {
    std::string result;
    bool inEscape = false;
    size_t start = 0, end = s.length();

    // Remove quotes
    if (s.length() >= 2 && (s[0] == '"' || s[0] == '\'')) {
        start = 1;
        end = s.length() - 1;
    }

    for (size_t i = start; i < end; ++i) {
        if (inEscape) {
            switch (s[i]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '\\': result += '\\'; break;
                case '\'': result += '\''; break;
                case '"': result += '"'; break;
                default: result += s[i]; break;
            }
            inEscape = false;
        } else if (s[i] == '\\') {
            inEscape = true;
        } else {
            result += s[i];
        }
    }
    return result;
}

std::any EvalVisitor::visitFile_input(Python3Parser::File_inputContext *ctx) {
    for (auto stmt : ctx->stmt()) {
        try {
            visit(stmt);
        } catch (const ReturnException&) {
            // Return at global scope
            break;
        }
    }
    return nullptr;
}

std::any EvalVisitor::visitFuncdef(Python3Parser::FuncdefContext *ctx) {
    std::string funcName = ctx->NAME()->getText();
    Function func;
    func.body = ctx->suite();

    if (ctx->parameters() && ctx->parameters()->typedargslist()) {
        auto paramsList = ctx->parameters()->typedargslist();
        auto params = paramsList->tfpdef();
        auto tests = paramsList->test();

        // In Python, parameters without defaults must come before those with defaults
        // If there are N parameters and M tests,
        // the first (N-M) parameters have no defaults,
        // and the last M parameters use the M tests as defaults in order
        size_t numParams = params.size();
        size_t numDefaults = tests.size();
        size_t numWithoutDefaults = numParams - numDefaults;

        for (size_t i = 0; i < numParams; ++i) {
            std::string paramName = params[i]->NAME()->getText();
            func.paramNames.push_back(paramName);

            if (i < numWithoutDefaults) {
                // No default value
                func.defaultValues.push_back(Value::makeNone());
            } else {
                // Has default value
                size_t defaultIdx = i - numWithoutDefaults;
                Value defaultVal = std::any_cast<Value>(visit(tests[defaultIdx]));
                func.defaultValues.push_back(defaultVal);
            }
        }
    }

    functions[funcName] = func;
    return nullptr;
}

std::any EvalVisitor::visitStmt(Python3Parser::StmtContext *ctx) {
    if (ctx->simple_stmt()) {
        return visit(ctx->simple_stmt());
    } else if (ctx->compound_stmt()) {
        return visit(ctx->compound_stmt());
    }
    return nullptr;
}

std::any EvalVisitor::visitSimple_stmt(Python3Parser::Simple_stmtContext *ctx) {
    return visit(ctx->small_stmt());
}

std::any EvalVisitor::visitSmall_stmt(Python3Parser::Small_stmtContext *ctx) {
    if (ctx->expr_stmt()) {
        return visit(ctx->expr_stmt());
    } else if (ctx->flow_stmt()) {
        return visit(ctx->flow_stmt());
    }
    return nullptr;
}

std::any EvalVisitor::visitExpr_stmt(Python3Parser::Expr_stmtContext *ctx) {
    auto testlists = ctx->testlist();

    if (testlists.size() == 1) {
        // Just an expression, evaluate it
        return visit(testlists[0]);
    }

    // Assignment
    if (ctx->getText().find("=") != std::string::npos && !ctx->augassign()) {
        // Regular assignment: a = b or a = b = c
        Value rightValue = std::any_cast<Value>(visit(testlists.back()));

        // Assign to all left-hand sides
        for (size_t i = 0; i < testlists.size() - 1; ++i) {
            auto testlist = testlists[i];
            auto tests = testlist->test();

            if (tests.size() == 1) {
                // Single assignment
                std::string varName = extractVarNameFromTest(tests[0]);
                if (!varName.empty()) {
                    setVariable(varName, rightValue);
                }
            } else {
                // Multiple assignment: a, b = 1, 2
                if (rightValue.type == ValueType::TUPLE && rightValue.tupleVal.size() == tests.size()) {
                    for (size_t j = 0; j < tests.size(); ++j) {
                        std::string varName = extractVarNameFromTest(tests[j]);
                        if (!varName.empty()) {
                            setVariable(varName, rightValue.tupleVal[j]);
                        }
                    }
                }
            }
        }
        return rightValue;
    } else if (ctx->augassign()) {
        // Augmented assignment: a += b
        std::string op = ctx->augassign()->getText();
        Value leftValue = std::any_cast<Value>(visit(testlists[0]));
        Value rightValue = std::any_cast<Value>(visit(testlists[1]));

        // Get variable name
        std::string varName = extractVarNameFromTest(testlists[0]->test(0));

        Value result;
        if (op == "+=") {
            if (leftValue.type == ValueType::INT && rightValue.type == ValueType::INT) {
                result = Value::makeInt(leftValue.intVal + rightValue.intVal);
            } else if (leftValue.type == ValueType::FLOAT || rightValue.type == ValueType::FLOAT) {
                double l = (leftValue.type == ValueType::FLOAT) ? leftValue.floatVal : leftValue.intVal.toDouble();
                double r = (rightValue.type == ValueType::FLOAT) ? rightValue.floatVal : rightValue.intVal.toDouble();
                result = Value::makeFloat(l + r);
            } else if (leftValue.type == ValueType::STRING && rightValue.type == ValueType::STRING) {
                result = Value::makeString(leftValue.stringVal + rightValue.stringVal);
            }
        } else if (op == "-=") {
            if (leftValue.type == ValueType::INT && rightValue.type == ValueType::INT) {
                result = Value::makeInt(leftValue.intVal - rightValue.intVal);
            } else {
                double l = (leftValue.type == ValueType::FLOAT) ? leftValue.floatVal : leftValue.intVal.toDouble();
                double r = (rightValue.type == ValueType::FLOAT) ? rightValue.floatVal : rightValue.intVal.toDouble();
                result = Value::makeFloat(l - r);
            }
        } else if (op == "*=") {
            if (leftValue.type == ValueType::INT && rightValue.type == ValueType::INT) {
                result = Value::makeInt(leftValue.intVal * rightValue.intVal);
            } else if (leftValue.type == ValueType::STRING && rightValue.type == ValueType::INT) {
                std::string repeated;
                long long count = rightValue.intVal.toDouble();
                for (long long j = 0; j < count; ++j) {
                    repeated += leftValue.stringVal;
                }
                result = Value::makeString(repeated);
            } else if (leftValue.type == ValueType::INT && rightValue.type == ValueType::STRING) {
                std::string repeated;
                long long count = leftValue.intVal.toDouble();
                for (long long j = 0; j < count; ++j) {
                    repeated += rightValue.stringVal;
                }
                result = Value::makeString(repeated);
            } else {
                double l = (leftValue.type == ValueType::FLOAT) ? leftValue.floatVal : leftValue.intVal.toDouble();
                double r = (rightValue.type == ValueType::FLOAT) ? rightValue.floatVal : rightValue.intVal.toDouble();
                result = Value::makeFloat(l * r);
            }
        } else if (op == "/=") {
            double l = (leftValue.type == ValueType::FLOAT) ? leftValue.floatVal : leftValue.intVal.toDouble();
            double r = (rightValue.type == ValueType::FLOAT) ? rightValue.floatVal : rightValue.intVal.toDouble();
            result = Value::makeFloat(l / r);
        } else if (op == "//=") {
            if (leftValue.type == ValueType::INT && rightValue.type == ValueType::INT) {
                result = Value::makeInt(leftValue.intVal / rightValue.intVal);
            } else {
                double l = (leftValue.type == ValueType::FLOAT) ? leftValue.floatVal : leftValue.intVal.toDouble();
                double r = (rightValue.type == ValueType::FLOAT) ? rightValue.floatVal : rightValue.intVal.toDouble();
                result = Value::makeFloat(std::floor(l / r));
            }
        } else if (op == "%=") {
            if (leftValue.type == ValueType::INT && rightValue.type == ValueType::INT) {
                result = Value::makeInt(leftValue.intVal % rightValue.intVal);
            } else {
                double l = (leftValue.type == ValueType::FLOAT) ? leftValue.floatVal : leftValue.intVal.toDouble();
                double r = (rightValue.type == ValueType::FLOAT) ? rightValue.floatVal : rightValue.intVal.toDouble();
                result = Value::makeFloat(l - std::floor(l / r) * r);
            }
        }

        if (!varName.empty()) {
            setVariable(varName, result);
        }
        return result;
    }

    return nullptr;
}

std::any EvalVisitor::visitAugassign(Python3Parser::AugassignContext *ctx) {
    return ctx->getText();
}

std::any EvalVisitor::visitFlow_stmt(Python3Parser::Flow_stmtContext *ctx) {
    if (ctx->break_stmt()) {
        return visit(ctx->break_stmt());
    } else if (ctx->continue_stmt()) {
        return visit(ctx->continue_stmt());
    } else if (ctx->return_stmt()) {
        return visit(ctx->return_stmt());
    }
    return nullptr;
}

std::any EvalVisitor::visitBreak_stmt(Python3Parser::Break_stmtContext *ctx) {
    throw BreakException();
}

std::any EvalVisitor::visitContinue_stmt(Python3Parser::Continue_stmtContext *ctx) {
    throw ContinueException();
}

std::any EvalVisitor::visitReturn_stmt(Python3Parser::Return_stmtContext *ctx) {
    if (ctx->testlist()) {
        Value val = std::any_cast<Value>(visit(ctx->testlist()));
        throw ReturnException(val);
    } else {
        throw ReturnException(Value::makeNone());
    }
}

std::any EvalVisitor::visitCompound_stmt(Python3Parser::Compound_stmtContext *ctx) {
    if (ctx->if_stmt()) {
        return visit(ctx->if_stmt());
    } else if (ctx->while_stmt()) {
        return visit(ctx->while_stmt());
    } else if (ctx->funcdef()) {
        return visit(ctx->funcdef());
    }
    return nullptr;
}

std::any EvalVisitor::visitIf_stmt(Python3Parser::If_stmtContext *ctx) {
    auto tests = ctx->test();
    auto suites = ctx->suite();

    // Check if condition
    Value condition = std::any_cast<Value>(visit(tests[0]));
    if (condition.toBool()) {
        visit(suites[0]);
        return nullptr;
    }

    // Check elif conditions
    for (size_t i = 1; i < tests.size(); ++i) {
        condition = std::any_cast<Value>(visit(tests[i]));
        if (condition.toBool()) {
            visit(suites[i]);
            return nullptr;
        }
    }

    // Else clause
    if (suites.size() > tests.size()) {
        visit(suites.back());
    }

    return nullptr;
}

std::any EvalVisitor::visitWhile_stmt(Python3Parser::While_stmtContext *ctx) {
    while (true) {
        Value condition = std::any_cast<Value>(visit(ctx->test()));
        if (!condition.toBool()) {
            break;
        }

        try {
            visit(ctx->suite());
        } catch (const BreakException&) {
            break;
        } catch (const ContinueException&) {
            continue;
        }
    }
    return nullptr;
}

std::any EvalVisitor::visitSuite(Python3Parser::SuiteContext *ctx) {
    if (ctx->simple_stmt()) {
        return visit(ctx->simple_stmt());
    } else {
        for (auto stmt : ctx->stmt()) {
            visit(stmt);
        }
    }
    return nullptr;
}

std::any EvalVisitor::visitTest(Python3Parser::TestContext *ctx) {
    return visit(ctx->or_test());
}

std::any EvalVisitor::visitOr_test(Python3Parser::Or_testContext *ctx) {
    auto and_tests = ctx->and_test();
    Value result = std::any_cast<Value>(visit(and_tests[0]));

    for (size_t i = 1; i < and_tests.size(); ++i) {
        if (result.toBool()) {
            return result; // Short-circuit
        }
        result = std::any_cast<Value>(visit(and_tests[i]));
    }

    return result;
}

std::any EvalVisitor::visitAnd_test(Python3Parser::And_testContext *ctx) {
    auto not_tests = ctx->not_test();
    Value result = std::any_cast<Value>(visit(not_tests[0]));

    for (size_t i = 1; i < not_tests.size(); ++i) {
        if (!result.toBool()) {
            return result; // Short-circuit
        }
        result = std::any_cast<Value>(visit(not_tests[i]));
    }

    return result;
}

std::any EvalVisitor::visitNot_test(Python3Parser::Not_testContext *ctx) {
    if (ctx->NOT()) {
        Value val = std::any_cast<Value>(visit(ctx->not_test()));
        return Value::makeBool(!val.toBool());
    }
    return visit(ctx->comparison());
}

std::any EvalVisitor::visitComparison(Python3Parser::ComparisonContext *ctx) {
    auto arith_exprs = ctx->arith_expr();
    auto comp_ops = ctx->comp_op();

    if (comp_ops.empty()) {
        return visit(arith_exprs[0]);
    }

    // Handle chained comparisons
    std::vector<Value> values;
    for (auto expr : arith_exprs) {
        values.push_back(std::any_cast<Value>(visit(expr)));
    }

    for (size_t i = 0; i < comp_ops.size(); ++i) {
        std::string op = comp_ops[i]->getText();
        Value left = values[i];
        Value right = values[i + 1];

        bool result = false;

        // Type conversion for comparison
        if (left.type == ValueType::INT && right.type == ValueType::INT) {
            if (op == "<") result = left.intVal < right.intVal;
            else if (op == ">") result = left.intVal > right.intVal;
            else if (op == "<=") result = left.intVal <= right.intVal;
            else if (op == ">=") result = left.intVal >= right.intVal;
            else if (op == "==") result = left.intVal == right.intVal;
            else if (op == "!=") result = left.intVal != right.intVal;
        } else if ((left.type == ValueType::INT || left.type == ValueType::FLOAT) &&
                   (right.type == ValueType::INT || right.type == ValueType::FLOAT)) {
            double l = (left.type == ValueType::FLOAT) ? left.floatVal : left.intVal.toDouble();
            double r = (right.type == ValueType::FLOAT) ? right.floatVal : right.intVal.toDouble();
            if (op == "<") result = l < r;
            else if (op == ">") result = l > r;
            else if (op == "<=") result = l <= r;
            else if (op == ">=") result = l >= r;
            else if (op == "==") result = l == r;
            else if (op == "!=") result = l != r;
        } else if (left.type == ValueType::STRING && right.type == ValueType::STRING) {
            if (op == "<") result = left.stringVal < right.stringVal;
            else if (op == ">") result = left.stringVal > right.stringVal;
            else if (op == "<=") result = left.stringVal <= right.stringVal;
            else if (op == ">=") result = left.stringVal >= right.stringVal;
            else if (op == "==") result = left.stringVal == right.stringVal;
            else if (op == "!=") result = left.stringVal != right.stringVal;
        } else if (left.type == ValueType::BOOL && right.type == ValueType::BOOL) {
            if (op == "==") result = left.boolVal == right.boolVal;
            else if (op == "!=") result = left.boolVal != right.boolVal;
        } else if (left.type == ValueType::NONE && right.type == ValueType::NONE) {
            if (op == "==") result = true;
            else if (op == "!=") result = false;
        } else {
            // Different types
            if (op == "==") result = false;
            else if (op == "!=") result = true;
        }

        if (!result) {
            return Value::makeBool(false);
        }
    }

    return Value::makeBool(true);
}

std::any EvalVisitor::visitComp_op(Python3Parser::Comp_opContext *ctx) {
    return ctx->getText();
}

std::any EvalVisitor::visitArith_expr(Python3Parser::Arith_exprContext *ctx) {
    auto terms = ctx->term();
    Value result = std::any_cast<Value>(visit(terms[0]));

    for (size_t i = 1; i < terms.size(); ++i) {
        std::string op = ctx->addorsub_op(i - 1)->getText();
        Value right = std::any_cast<Value>(visit(terms[i]));

        if (op == "+") {
            if (result.type == ValueType::INT && right.type == ValueType::INT) {
                result = Value::makeInt(result.intVal + right.intVal);
            } else if (result.type == ValueType::FLOAT || right.type == ValueType::FLOAT) {
                double l = (result.type == ValueType::FLOAT) ? result.floatVal : result.intVal.toDouble();
                double r = (right.type == ValueType::FLOAT) ? right.floatVal : right.intVal.toDouble();
                result = Value::makeFloat(l + r);
            } else if (result.type == ValueType::STRING && right.type == ValueType::STRING) {
                result = Value::makeString(result.stringVal + right.stringVal);
            }
        } else if (op == "-") {
            if (result.type == ValueType::INT && right.type == ValueType::INT) {
                result = Value::makeInt(result.intVal - right.intVal);
            } else {
                double l = (result.type == ValueType::FLOAT) ? result.floatVal : result.intVal.toDouble();
                double r = (right.type == ValueType::FLOAT) ? right.floatVal : right.intVal.toDouble();
                result = Value::makeFloat(l - r);
            }
        }
    }

    return result;
}

std::any EvalVisitor::visitAddorsub_op(Python3Parser::Addorsub_opContext *ctx) {
    return ctx->getText();
}

std::any EvalVisitor::visitTerm(Python3Parser::TermContext *ctx) {
    auto factors = ctx->factor();
    Value result = std::any_cast<Value>(visit(factors[0]));

    for (size_t i = 1; i < factors.size(); ++i) {
        std::string op = ctx->muldivmod_op(i - 1)->getText();
        Value right = std::any_cast<Value>(visit(factors[i]));

        if (op == "*") {
            if (result.type == ValueType::INT && right.type == ValueType::INT) {
                result = Value::makeInt(result.intVal * right.intVal);
            } else if (result.type == ValueType::FLOAT || right.type == ValueType::FLOAT) {
                double l = (result.type == ValueType::FLOAT) ? result.floatVal : result.intVal.toDouble();
                double r = (right.type == ValueType::FLOAT) ? right.floatVal : right.intVal.toDouble();
                result = Value::makeFloat(l * r);
            } else if (result.type == ValueType::STRING && right.type == ValueType::INT) {
                std::string repeated;
                long long count = right.intVal.toDouble();
                for (long long j = 0; j < count; ++j) {
                    repeated += result.stringVal;
                }
                result = Value::makeString(repeated);
            } else if (result.type == ValueType::INT && right.type == ValueType::STRING) {
                std::string repeated;
                long long count = result.intVal.toDouble();
                for (long long j = 0; j < count; ++j) {
                    repeated += right.stringVal;
                }
                result = Value::makeString(repeated);
            }
        } else if (op == "/") {
            double l = (result.type == ValueType::FLOAT) ? result.floatVal : result.intVal.toDouble();
            double r = (right.type == ValueType::FLOAT) ? right.floatVal : right.intVal.toDouble();
            result = Value::makeFloat(l / r);
        } else if (op == "//") {
            if (result.type == ValueType::INT && right.type == ValueType::INT) {
                result = Value::makeInt(result.intVal / right.intVal);
            } else {
                double l = (result.type == ValueType::FLOAT) ? result.floatVal : result.intVal.toDouble();
                double r = (right.type == ValueType::FLOAT) ? right.floatVal : right.intVal.toDouble();
                result = Value::makeFloat(std::floor(l / r));
            }
        } else if (op == "%") {
            if (result.type == ValueType::INT && right.type == ValueType::INT) {
                result = Value::makeInt(result.intVal % right.intVal);
            } else {
                double l = (result.type == ValueType::FLOAT) ? result.floatVal : result.intVal.toDouble();
                double r = (right.type == ValueType::FLOAT) ? right.floatVal : right.intVal.toDouble();
                result = Value::makeFloat(l - std::floor(l / r) * r);
            }
        }
    }

    return result;
}

std::any EvalVisitor::visitMuldivmod_op(Python3Parser::Muldivmod_opContext *ctx) {
    return ctx->getText();
}

std::any EvalVisitor::visitFactor(Python3Parser::FactorContext *ctx) {
    if (ctx->atom_expr()) {
        return visit(ctx->atom_expr());
    }

    // Unary operators
    Value val = std::any_cast<Value>(visit(ctx->factor()));
    if (ctx->getText()[0] == '+') {
        return val;
    } else if (ctx->getText()[0] == '-') {
        if (val.type == ValueType::INT) {
            return Value::makeInt(-val.intVal);
        } else if (val.type == ValueType::FLOAT) {
            return Value::makeFloat(-val.floatVal);
        }
    }

    return val;
}

std::any EvalVisitor::visitAtom_expr(Python3Parser::Atom_exprContext *ctx) {
    Value base = std::any_cast<Value>(visit(ctx->atom()));

    auto trailer = ctx->trailer();
    if (trailer) {
        // Function call
        if (base.type == ValueType::STRING) {
            std::string funcName = base.stringVal;
            std::vector<Value> args;
            std::map<std::string, Value> kwargs;

            if (trailer->arglist()) {
                auto arguments = trailer->arglist()->argument();
                for (size_t i = 0; i < arguments.size(); ++i) {
                    auto arg = arguments[i];
                    if (arg->test().size() == 2) {
                        // Keyword argument
                        std::string key = arg->test(0)->getText();
                        Value val = std::any_cast<Value>(visit(arg->test(1)));
                        kwargs[key] = val;
                    } else {
                        // Positional argument
                        Value val = std::any_cast<Value>(visit(arg->test(0)));
                        args.push_back(val);
                    }
                }
            }

            // Check if it's a built-in function
            if (funcName == "print" || funcName == "int" || funcName == "float" || funcName == "str" || funcName == "bool") {
                base = callBuiltinFunction(funcName, args);
            } else {
                base = callFunction(funcName, args, kwargs);
            }
        }
    }

    return base;
}

std::any EvalVisitor::visitTrailer(Python3Parser::TrailerContext *ctx) {
    return visitChildren(ctx);
}

std::any EvalVisitor::visitAtom(Python3Parser::AtomContext *ctx) {
    // Check for boolean and None literals
    if (ctx->TRUE()) {
        return Value::makeBool(true);
    }
    if (ctx->FALSE()) {
        return Value::makeBool(false);
    }
    if (ctx->NONE()) {
        return Value::makeNone();
    }

    if (ctx->NAME()) {
        std::string name = ctx->NAME()->getText();

        // Check if it's a function name (for function calls)
        if (functions.count(name)) {
            return Value::makeString(name);
        }

        // Check for built-in functions
        if (name == "print" || name == "int" || name == "float" || name == "str" || name == "bool") {
            return Value::makeString(name);
        }

        // Variable
        return getVariable(name);
    }

    if (ctx->NUMBER()) {
        std::string num = ctx->NUMBER()->getText();
        if (num.find('.') != std::string::npos) {
            return Value::makeFloat(std::stod(num));
        } else {
            return Value::makeInt(BigInteger(num));
        }
    }

    if (ctx->STRING().size() > 0) {
        std::string result;
        for (auto str : ctx->STRING()) {
            result += parseStringLiteral(str->getText());
        }
        return Value::makeString(result);
    }

    if (ctx->format_string()) {
        return visit(ctx->format_string());
    }

    if (ctx->test()) {
        return visit(ctx->test());
    }

    return Value::makeNone();
}

std::any EvalVisitor::visitFormat_string(Python3Parser::Format_stringContext *ctx) {
    std::string text = ctx->getText();
    std::string result;

    // Remove f" and "
    size_t start = 2;
    size_t end = text.length() - 1;

    size_t i = start;
    while (i < end) {
        if (text[i] == '{') {
            if (i + 1 < end && text[i + 1] == '{') {
                result += '{';
                i += 2;
            } else {
                // Find matching }
                int braceCount = 1;
                size_t j = i + 1;
                while (j < end && braceCount > 0) {
                    if (text[j] == '{') braceCount++;
                    else if (text[j] == '}') braceCount--;
                    j++;
                }

                std::string expr = text.substr(i + 1, j - i - 2);

                // Parse and evaluate the expression
                ANTLRInputStream input(expr);
                Python3Lexer lexer(&input);
                CommonTokenStream tokens(&lexer);
                Python3Parser parser(&tokens);
                auto tree = parser.test();
                Value val = std::any_cast<Value>(visit(tree));

                // Convert value to string for f-string
                if (val.type == ValueType::BOOL) {
                    result += val.boolVal ? "True" : "False";
                } else {
                    result += val.toString();
                }

                i = j;
            }
        } else if (text[i] == '}') {
            if (i + 1 < end && text[i + 1] == '}') {
                result += '}';
                i += 2;
            } else {
                result += text[i];
                i++;
            }
        } else {
            result += text[i];
            i++;
        }
    }

    return Value::makeString(result);
}

std::any EvalVisitor::visitTestlist(Python3Parser::TestlistContext *ctx) {
    auto tests = ctx->test();

    if (tests.size() == 1) {
        return visit(tests[0]);
    }

    // Multiple values - create a tuple
    std::vector<Value> values;
    for (auto test : tests) {
        values.push_back(std::any_cast<Value>(visit(test)));
    }
    return Value::makeTuple(values);
}

std::any EvalVisitor::visitArglist(Python3Parser::ArglistContext *ctx) {
    return visitChildren(ctx);
}

std::any EvalVisitor::visitArgument(Python3Parser::ArgumentContext *ctx) {
    return visitChildren(ctx);
}

std::any EvalVisitor::visitParameters(Python3Parser::ParametersContext *ctx) {
    return visitChildren(ctx);
}

std::any EvalVisitor::visitTypedargslist(Python3Parser::TypedargslistContext *ctx) {
    return visitChildren(ctx);
}

Value EvalVisitor::callBuiltinFunction(const std::string& name, const std::vector<Value>& args) {
    if (name == "print") {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cout << " ";
            Value arg = args[i];
            if (arg.type == ValueType::BOOL) {
                std::cout << (arg.boolVal ? "True" : "False");
            } else {
                std::cout << arg.toString();
            }
        }
        std::cout << std::endl;
        return Value::makeNone();
    } else if (name == "int") {
        if (args.empty()) return Value::makeInt(BigInteger(0));
        return args[0].convertToInt();
    } else if (name == "float") {
        if (args.empty()) return Value::makeFloat(0.0);
        return args[0].convertToFloat();
    } else if (name == "str") {
        if (args.empty()) return Value::makeString("");
        return args[0].convertToString();
    } else if (name == "bool") {
        if (args.empty()) return Value::makeBool(false);
        return args[0].convertToBool();
    }

    return Value::makeNone();
}

Value EvalVisitor::callFunction(const std::string& name, const std::vector<Value>& args,
                                 const std::map<std::string, Value>& kwargs) {
    if (!functions.count(name)) {
        return Value::makeNone();
    }

    Function& func = functions[name];

    // Create new scope
    pushScope();

    // Bind arguments
    for (size_t i = 0; i < func.paramNames.size(); ++i) {
        Value argValue;

        // Check if provided as positional argument
        if (i < args.size()) {
            argValue = args[i];
        }
        // Check if provided as keyword argument
        else if (kwargs.count(func.paramNames[i])) {
            argValue = kwargs.at(func.paramNames[i]);
        }
        // Use default value
        else if (i < func.defaultValues.size() && func.defaultValues[i].type != ValueType::NONE) {
            argValue = func.defaultValues[i];
        } else {
            argValue = Value::makeNone();
        }

        setVariable(func.paramNames[i], argValue);
    }

    // Execute function body
    Value returnValue = Value::makeNone();
    try {
        visit(func.body);
    } catch (const ReturnException& e) {
        returnValue = e.value;
    }

    // Pop scope
    popScope();

    return returnValue;
}
