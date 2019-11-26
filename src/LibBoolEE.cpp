/******************************************************************************
Created by Adam Streck, 2016, adam.streck@gmail.com

This file is part of the LibBoolEE library.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "LibBoolEE.h"

std::vector<std::string> LibBoolEE::singleParse(const std::string & formula, const char op, ErrorReport* errorReport) {
    int start_pos = -1;
    int parity_count = 0;
    std::vector<std::string> subexpressions;
    for (int i = 0; i < static_cast<int>(formula.size()); i++) {
        if (formula[i] == ')') {
            parity_count--;
        }
        else if (formula[i] == '(') {
            parity_count++;
            if (start_pos == -1) {
                start_pos = i;
            }
        }
        else if (parity_count == 0) {
            if (start_pos == -1) {
                if (belongsToName(formula[i]) || formula[i] == '!') {
                    start_pos = i;
                }
            }
            else if (!(belongsToName(formula[i]) || formula[i] == '!')) {
                if (op == formula[i]) {
                    subexpressions.push_back(formula.substr(start_pos, i - start_pos));
                    start_pos = i+1;
                }
                else if (formula[i] != '&' && formula[i] != '|') {
                    if (errorReport) {
                        errorReport->type = ErrorReport::ErrorType::EmptySubExpression;
                        errorReport->vecUserData.emplace_back(std::string(1, formula[i]));
                        errorReport->vecUserData.emplace_back(formula);
                        errorReport->strDevData = "invalid-verifier-unknown-operator-in-expression";
                    }
                    throw std::runtime_error("Unknown operator '" + std::string(1, formula[i]) + "' in the (sub)expression '" + formula + "'.");
                }
            }
        }
    }
    if (start_pos != -1) {
        subexpressions.push_back(formula.substr(start_pos, formula.size() - start_pos));
    }
    if (parity_count != 0) {
        if (errorReport) {
            errorReport->type = ErrorReport::ErrorType::ParenthesisParity;
            errorReport->vecUserData.emplace_back(formula);
            errorReport->strDevData = "invalid-verifier-parenthesis-parity";
        }
        throw std::runtime_error("Wrong parenthesis parity in the (sub)expression '" + formula + "'.");
    }
    return subexpressions;
}

bool LibBoolEE::resolve(const std::string &source, const Vals & valuation, ErrorReport* errorReport) {
    return resolveRec(removeWhitespaces(source), valuation, errorReport);
}

bool LibBoolEE::resolveRec(const std::string &source, const Vals & valuation, ErrorReport* errorReport) {
    if (source.empty()) {
        if (errorReport) {
            errorReport->type = ErrorReport::ErrorType::EmptySubExpression;
            errorReport->vecUserData.emplace_back(source);
            errorReport->strDevData = "bad-txns-null-verifier-empty-sub-expression";
        }
        throw std::runtime_error("An empty subexpression was encountered");
    }

    std::string formula = source;
    char current_op = '|';
    // Try to divide by |
    std::vector<std::string> subexpressions = singleParse(source, current_op, errorReport);
    // No | on the top level
    if (subexpressions.size() == 1) {
        current_op = '&';
        subexpressions = singleParse(source, current_op, errorReport);
    }

    // No valid name found
    if (subexpressions.size() == 0) {
        if (errorReport) {
            errorReport->type = ErrorReport::ErrorType::InvalidQualifierName;
            errorReport->vecUserData.emplace_back(source);
            errorReport->strDevData = "bad-txns-null-verifier-no-sub-expressions";
        }
        throw std::runtime_error("The subexpression " + source + " is not a valid formula.");
    }

    // No binary top level operator found
    else if (subexpressions.size() == 1) {
        if (source[0] == '!') {
            return !resolve(source.substr(1), valuation, errorReport);
        }
        else if (source[0] == '(') {
            return resolve(source.substr(1, source.size() - 2), valuation, errorReport);
        }
        else if (source == "1") {
            return true;
        }
        else if (source == "0") {
            return false;
        }
        else if (valuation.count(source) == 0) {
            if (errorReport) {
                errorReport->type = ErrorReport::ErrorType::VariableNotFound;
                errorReport->vecUserData.emplace_back(source);
                errorReport->strDevData = "bad-txns-null-verifier-variable-not-found";
            }
            throw std::runtime_error("Variable '" + source + "' not found in the interpretation.");
        }
        else {
            return valuation.at(source);
        }
    }
    else {
        if (current_op == '|') {
            bool result = false;
            for (std::vector<std::string>::iterator it = subexpressions.begin(); it != subexpressions.end(); it++) {
                result |= resolve(*it, valuation, errorReport);
            }
            return result;
        }
        else { // The operator was set to &
            bool result = true;
            for (std::vector<std::string>::iterator it = subexpressions.begin(); it != subexpressions.end(); it++) {
                result &= resolve(*it, valuation, errorReport);
            }
            return result;
        }
    }
}

std::string LibBoolEE::trim(const std::string &source) {
    static const std::string WHITESPACES = " \n\r\t\v\f";
    const size_t front = source.find_first_not_of(WHITESPACES);
    return source.substr(front, source.find_last_not_of(WHITESPACES) - front + 1);
}

bool LibBoolEE::belongsToName(const char ch) {
    return isalnum(ch) || ch == '_' || ch == '#' || ch == '.';
}

std::string LibBoolEE::removeWhitespaces(const std::string &source) {
    std::string result;
    for (int i = 0; i < static_cast<int>(source.size()); i++) {
        if (!isspace(source.at(i))) {
            result += source.at(i);
        }
    }
    return result;
}

std::string LibBoolEE::removeCharacter(const std::string &source, const char ch) {
    std::string result;
    for (int i = 0; i < static_cast<int>(source.size()); i++) {
        if (ch != source.at(i)) {
            result += source.at(i);
        }
    }
    return result;
}

