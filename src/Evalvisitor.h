#pragma once
#ifndef PYTHON_INTERPRETER_EVALVISITOR_H
#define PYTHON_INTERPRETER_EVALVISITOR_H

#include "Python3ParserBaseVisitor.h"
#include "Value.h"
#include "BigInteger.h"
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <exception>

// Control flow exceptions
class BreakException : public std::exception {};
class ContinueException : public std::exception {};
class ReturnException : public std::exception {
public:
    Value value;
    ReturnException(const Value& v) : value(v) {}
};

// Function definition
struct Function {
    std::vector<std::string> paramNames;
    std::vector<Value> defaultValues;
    Python3Parser::SuiteContext* body;

    Function() : body(nullptr) {}
};

class EvalVisitor : public Python3ParserBaseVisitor {
private:
    std::map<std::string, Value> globalVars;
    std::vector<std::map<std::string, Value>> localScopes;
    std::map<std::string, Function> functions;

    bool inFunction = false;

    void setVariable(const std::string& name, const Value& value) {
        if (!localScopes.empty()) {
            localScopes.back()[name] = value;
        } else {
            globalVars[name] = value;
        }
    }

    Value getVariable(const std::string& name) {
        // Check local scopes first (from innermost to outermost)
        for (int i = localScopes.size() - 1; i >= 0; --i) {
            if (localScopes[i].count(name)) {
                return localScopes[i][name];
            }
        }
        // Then check global variables
        if (globalVars.count(name)) {
            return globalVars[name];
        }
        return Value::makeNone();
    }

    void pushScope() {
        localScopes.push_back(std::map<std::string, Value>());
    }

    void popScope() {
        if (!localScopes.empty()) {
            localScopes.pop_back();
        }
    }

    std::string extractVarName(Python3Parser::TestContext* test);

public:
    std::any visitFile_input(Python3Parser::File_inputContext *ctx) override;
    std::any visitFuncdef(Python3Parser::FuncdefContext *ctx) override;
    std::any visitParameters(Python3Parser::ParametersContext *ctx) override;
    std::any visitTypedargslist(Python3Parser::TypedargslistContext *ctx) override;
    std::any visitStmt(Python3Parser::StmtContext *ctx) override;
    std::any visitSimple_stmt(Python3Parser::Simple_stmtContext *ctx) override;
    std::any visitSmall_stmt(Python3Parser::Small_stmtContext *ctx) override;
    std::any visitExpr_stmt(Python3Parser::Expr_stmtContext *ctx) override;
    std::any visitAugassign(Python3Parser::AugassignContext *ctx) override;
    std::any visitFlow_stmt(Python3Parser::Flow_stmtContext *ctx) override;
    std::any visitBreak_stmt(Python3Parser::Break_stmtContext *ctx) override;
    std::any visitContinue_stmt(Python3Parser::Continue_stmtContext *ctx) override;
    std::any visitReturn_stmt(Python3Parser::Return_stmtContext *ctx) override;
    std::any visitCompound_stmt(Python3Parser::Compound_stmtContext *ctx) override;
    std::any visitIf_stmt(Python3Parser::If_stmtContext *ctx) override;
    std::any visitWhile_stmt(Python3Parser::While_stmtContext *ctx) override;
    std::any visitSuite(Python3Parser::SuiteContext *ctx) override;
    std::any visitTest(Python3Parser::TestContext *ctx) override;
    std::any visitOr_test(Python3Parser::Or_testContext *ctx) override;
    std::any visitAnd_test(Python3Parser::And_testContext *ctx) override;
    std::any visitNot_test(Python3Parser::Not_testContext *ctx) override;
    std::any visitComparison(Python3Parser::ComparisonContext *ctx) override;
    std::any visitComp_op(Python3Parser::Comp_opContext *ctx) override;
    std::any visitArith_expr(Python3Parser::Arith_exprContext *ctx) override;
    std::any visitAddorsub_op(Python3Parser::Addorsub_opContext *ctx) override;
    std::any visitTerm(Python3Parser::TermContext *ctx) override;
    std::any visitMuldivmod_op(Python3Parser::Muldivmod_opContext *ctx) override;
    std::any visitFactor(Python3Parser::FactorContext *ctx) override;
    std::any visitAtom_expr(Python3Parser::Atom_exprContext *ctx) override;
    std::any visitTrailer(Python3Parser::TrailerContext *ctx) override;
    std::any visitAtom(Python3Parser::AtomContext *ctx) override;
    std::any visitFormat_string(Python3Parser::Format_stringContext *ctx) override;
    std::any visitTestlist(Python3Parser::TestlistContext *ctx) override;
    std::any visitArglist(Python3Parser::ArglistContext *ctx) override;
    std::any visitArgument(Python3Parser::ArgumentContext *ctx) override;

    Value callBuiltinFunction(const std::string& name, const std::vector<Value>& args);
    Value callFunction(const std::string& name, const std::vector<Value>& args,
                       const std::map<std::string, Value>& kwargs);
};

#endif//PYTHON_INTERPRETER_EVALVISITOR_H
