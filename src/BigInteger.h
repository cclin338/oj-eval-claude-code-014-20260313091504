#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>

class BigInteger {
private:
    std::vector<int> digits; // Store digits in reverse order (least significant first)
    bool negative;

    void removeLeadingZeros() {
        while (digits.size() > 1 && digits.back() == 0) {
            digits.pop_back();
        }
        if (digits.size() == 1 && digits[0] == 0) {
            negative = false;
        }
    }

    static BigInteger absAdd(const BigInteger& a, const BigInteger& b) {
        BigInteger result;
        result.negative = false;
        int carry = 0;
        size_t maxSize = std::max(a.digits.size(), b.digits.size());
        result.digits.resize(maxSize);

        for (size_t i = 0; i < maxSize || carry; ++i) {
            if (i == result.digits.size()) result.digits.push_back(0);
            int sum = carry;
            if (i < a.digits.size()) sum += a.digits[i];
            if (i < b.digits.size()) sum += b.digits[i];
            result.digits[i] = sum % 10;
            carry = sum / 10;
        }
        result.removeLeadingZeros();
        return result;
    }

    static BigInteger absSub(const BigInteger& a, const BigInteger& b) {
        BigInteger result;
        result.negative = false;
        int carry = 0;
        result.digits.resize(a.digits.size());

        for (size_t i = 0; i < a.digits.size(); ++i) {
            int diff = a.digits[i] - carry;
            if (i < b.digits.size()) diff -= b.digits[i];
            if (diff < 0) {
                diff += 10;
                carry = 1;
            } else {
                carry = 0;
            }
            result.digits[i] = diff;
        }
        result.removeLeadingZeros();
        return result;
    }

    static int absCompare(const BigInteger& a, const BigInteger& b) {
        if (a.digits.size() != b.digits.size()) {
            return a.digits.size() < b.digits.size() ? -1 : 1;
        }
        for (int i = a.digits.size() - 1; i >= 0; --i) {
            if (a.digits[i] != b.digits[i]) {
                return a.digits[i] < b.digits[i] ? -1 : 1;
            }
        }
        return 0;
    }

public:
    BigInteger() : negative(false) {
        digits.push_back(0);
    }

    BigInteger(long long num) {
        if (num == 0) {
            digits.push_back(0);
            negative = false;
        } else {
            negative = num < 0;
            if (negative) num = -num;
            while (num > 0) {
                digits.push_back(num % 10);
                num /= 10;
            }
        }
    }

    BigInteger(const std::string& str) {
        if (str.empty() || str == "0") {
            digits.push_back(0);
            negative = false;
            return;
        }

        size_t start = 0;
        negative = (str[0] == '-');
        if (negative || str[0] == '+') start = 1;

        for (int i = str.length() - 1; i >= (int)start; --i) {
            digits.push_back(str[i] - '0');
        }
        removeLeadingZeros();
    }

    bool isZero() const {
        return digits.size() == 1 && digits[0] == 0;
    }

    bool isNegative() const {
        return negative;
    }

    BigInteger operator+(const BigInteger& other) const {
        if (negative == other.negative) {
            BigInteger result = absAdd(*this, other);
            result.negative = negative;
            return result;
        } else {
            int cmp = absCompare(*this, other);
            if (cmp == 0) return BigInteger(0);
            if (cmp > 0) {
                BigInteger result = absSub(*this, other);
                result.negative = negative;
                return result;
            } else {
                BigInteger result = absSub(other, *this);
                result.negative = other.negative;
                return result;
            }
        }
    }

    BigInteger operator-(const BigInteger& other) const {
        BigInteger negOther = other;
        negOther.negative = !other.negative;
        if (negOther.isZero()) negOther.negative = false;
        return *this + negOther;
    }

    BigInteger operator*(const BigInteger& other) const {
        BigInteger result;
        result.digits.resize(digits.size() + other.digits.size(), 0);

        for (size_t i = 0; i < digits.size(); ++i) {
            int carry = 0;
            for (size_t j = 0; j < other.digits.size() || carry; ++j) {
                long long cur = result.digits[i + j] +
                                digits[i] * 1LL * (j < other.digits.size() ? other.digits[j] : 0) + carry;
                result.digits[i + j] = cur % 10;
                carry = cur / 10;
            }
        }

        result.removeLeadingZeros();
        result.negative = (negative != other.negative) && !result.isZero();
        return result;
    }

    BigInteger operator/(const BigInteger& other) const {
        if (other.isZero()) {
            throw std::runtime_error("Division by zero");
        }

        BigInteger dividend = *this;
        dividend.negative = false;
        BigInteger divisor = other;
        divisor.negative = false;

        if (absCompare(dividend, divisor) < 0) {
            if (negative != other.negative && !isZero()) {
                return BigInteger(-1);
            }
            return BigInteger(0);
        }

        BigInteger result;
        result.digits.clear();
        BigInteger current;
        current.digits.clear();

        for (int i = digits.size() - 1; i >= 0; --i) {
            current.digits.insert(current.digits.begin(), digits[i]);
            current.removeLeadingZeros();

            int x = 0, l = 0, r = 10;
            while (l <= r) {
                int m = (l + r) / 2;
                BigInteger t = divisor * BigInteger(m);
                if (absCompare(t, current) <= 0) {
                    x = m;
                    l = m + 1;
                } else {
                    r = m - 1;
                }
            }
            result.digits.insert(result.digits.begin(), x);
            current = current - divisor * BigInteger(x);
        }

        result.removeLeadingZeros();

        // Floor division: adjust for negative results
        if (negative != other.negative && !result.isZero()) {
            if (!current.isZero()) {
                result = result + BigInteger(1);
            }
            result.negative = true;
        }

        return result;
    }

    BigInteger operator%(const BigInteger& other) const {
        // a % b = a - (a // b) * b
        BigInteger quotient = *this / other;
        BigInteger result = *this - quotient * other;
        return result;
    }

    bool operator<(const BigInteger& other) const {
        if (negative != other.negative) return negative;
        int cmp = absCompare(*this, other);
        return negative ? cmp > 0 : cmp < 0;
    }

    bool operator>(const BigInteger& other) const {
        return other < *this;
    }

    bool operator<=(const BigInteger& other) const {
        return !(other < *this);
    }

    bool operator>=(const BigInteger& other) const {
        return !(*this < other);
    }

    bool operator==(const BigInteger& other) const {
        return negative == other.negative && digits == other.digits;
    }

    bool operator!=(const BigInteger& other) const {
        return !(*this == other);
    }

    BigInteger operator-() const {
        BigInteger result = *this;
        if (!isZero()) {
            result.negative = !negative;
        }
        return result;
    }

    std::string toString() const {
        std::string result;
        if (negative) result += '-';
        for (int i = digits.size() - 1; i >= 0; --i) {
            result += char('0' + digits[i]);
        }
        return result;
    }

    double toDouble() const {
        double result = 0;
        double base = 1;
        for (size_t i = 0; i < digits.size(); ++i) {
            result += digits[i] * base;
            base *= 10;
        }
        return negative ? -result : result;
    }
};
