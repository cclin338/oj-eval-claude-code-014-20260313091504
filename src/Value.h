#pragma once
#include "BigInteger.h"
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>

enum class ValueType {
    NONE,
    BOOL,
    INT,
    FLOAT,
    STRING,
    TUPLE
};

class Value {
public:
    ValueType type;
    BigInteger intVal;
    double floatVal;
    std::string stringVal;
    bool boolVal;
    std::vector<Value> tupleVal;

    Value() : type(ValueType::NONE) {}

    Value(ValueType t) : type(t) {
        if (t == ValueType::INT) intVal = BigInteger(0);
        if (t == ValueType::FLOAT) floatVal = 0.0;
        if (t == ValueType::BOOL) boolVal = false;
    }

    static Value makeNone() {
        return Value(ValueType::NONE);
    }

    static Value makeBool(bool b) {
        Value v(ValueType::BOOL);
        v.boolVal = b;
        return v;
    }

    static Value makeInt(const BigInteger& i) {
        Value v(ValueType::INT);
        v.intVal = i;
        return v;
    }

    static Value makeFloat(double f) {
        Value v(ValueType::FLOAT);
        v.floatVal = f;
        return v;
    }

    static Value makeString(const std::string& s) {
        Value v(ValueType::STRING);
        v.stringVal = s;
        return v;
    }

    static Value makeTuple(const std::vector<Value>& vals) {
        Value v(ValueType::TUPLE);
        v.tupleVal = vals;
        return v;
    }

    bool toBool() const {
        switch (type) {
            case ValueType::NONE:
                return false;
            case ValueType::BOOL:
                return boolVal;
            case ValueType::INT:
                return !intVal.isZero();
            case ValueType::FLOAT:
                return floatVal != 0.0;
            case ValueType::STRING:
                return !stringVal.empty();
            case ValueType::TUPLE:
                return !tupleVal.empty();
        }
        return false;
    }

    std::string toString() const {
        std::ostringstream oss;
        switch (type) {
            case ValueType::NONE:
                return "None";
            case ValueType::BOOL:
                return boolVal ? "True" : "False";
            case ValueType::INT:
                return intVal.toString();
            case ValueType::FLOAT:
                oss << std::fixed << std::setprecision(6) << floatVal;
                return oss.str();
            case ValueType::STRING:
                return stringVal;
            case ValueType::TUPLE:
                oss << "(";
                for (size_t i = 0; i < tupleVal.size(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << tupleVal[i].toString();
                }
                if (tupleVal.size() == 1) oss << ",";
                oss << ")";
                return oss.str();
        }
        return "";
    }

    Value convertToInt() const {
        switch (type) {
            case ValueType::INT:
                return *this;
            case ValueType::FLOAT:
                return Value::makeInt(BigInteger((long long)floatVal));
            case ValueType::BOOL:
                return Value::makeInt(BigInteger(boolVal ? 1 : 0));
            case ValueType::STRING: {
                try {
                    return Value::makeInt(BigInteger(stringVal));
                } catch (...) {
                    return Value::makeInt(BigInteger(0));
                }
            }
            default:
                return Value::makeInt(BigInteger(0));
        }
    }

    Value convertToFloat() const {
        switch (type) {
            case ValueType::FLOAT:
                return *this;
            case ValueType::INT:
                return Value::makeFloat(intVal.toDouble());
            case ValueType::BOOL:
                return Value::makeFloat(boolVal ? 1.0 : 0.0);
            case ValueType::STRING: {
                try {
                    return Value::makeFloat(std::stod(stringVal));
                } catch (...) {
                    return Value::makeFloat(0.0);
                }
            }
            default:
                return Value::makeFloat(0.0);
        }
    }

    Value convertToString() const {
        return Value::makeString(toString());
    }

    Value convertToBool() const {
        return Value::makeBool(toBool());
    }
};
