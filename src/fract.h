#ifndef FRACT_H
#define FRACT_H

#include <numeric>  // For std::gcd
#include <nlohmann/json.hpp>

// like all gpt

struct fract {
    int num;  // Numerator
    int den;  // Denominator

    // Constructor to initialize the fraction
    fract(int n, int d) : num(n), den(d == 0 ? 1 : d) {}

    double operator*(double factor) const {
        return factor*double(num)/den;
    }

    // Default constructor (fraction 0/1)
    fract() : num(0), den(1) {}

    // Convert fract to double
    operator double() const {
        return static_cast<double>(num) / den;
    }

    // Simplify the fraction (reduce numerator and denominator)
    void simplify() {
        int gcd_value = std::gcd(num, den);
        num /= gcd_value;
        den /= gcd_value;
    }

    // Overload the '+' operator to add two fractions
    fract operator+(const fract& other) const {
        // Find common denominator
        int common_den = den * other.den;

        // Adjust numerators to have the common denominator
        int new_num = (num * other.den) + (other.num * den);

        // Return the simplified result
        fract result(new_num, common_den);
        result.simplify();
        return result;
    }

    fract operator-(const fract& other) const {
        // Find common denominator
        int common_den = den * other.den;

        // Adjust numerators to have the common denominator
        int new_num = (num * other.den) - (other.num * den);

        // Return the simplified result
        fract result(new_num, common_den);
        result.simplify();
        return result;
    }

    // Overload the '+' operator to add a fract and a double
    fract operator+(double value) const {
        // Convert fract to double and add to the given value
        double result = static_cast<double>(*this) + value;

        // Convert back to a fraction (assuming we want to convert result to fract)
        // For simplicity, we'll use a large denominator (like 10000) to get an approximation
        int new_num = static_cast<int>(result * 10000);
        int new_den = 10000;
        
        // Return the new fract (unsimplified)
        fract new_fract(new_num, new_den);
        new_fract.simplify();
        return new_fract;
    }

    nlohmann::json toJSON() {
        return nlohmann::json{{"num", num}, {"den", den}};
    }

    static fract fromJSON(const nlohmann::json& j) {
        fract f{};
        j.at("num").get_to(f.num);
        j.at("den").get_to(f.den);
        if (f.den == 0) f.den = 1;
        return f;
    }
};

#endif
