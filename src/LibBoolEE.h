#ifndef LIBBOOLEE_H
#define LIBBOOLEE_H

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Class able to resolve any logical function in propositional logic.
///
/// This is a static helper class able of resolving any preposition logic formula.
/// Formula construction:
///   -# \f$tt\f$ (true) and \f$ff\f$ (false) are formulas representing true and false respectively,
///	  -# any variable is a formula,
///   -# for \f$\varphi\f$ formula is \f$!\varphi\f$ formula,
///   -# for \f$\psi, \varphi\f$ formulas are \f$(\psi|\varphi)\f$, \f$(\psi\&\varphi)\f$ formulas representing logical disjunction and conjunction respectively,
///   -# nothing else is a formula,
///   -# whitespaces are ignored.
///
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "assets/assets.h"

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

class LibBoolEE {
public:
    typedef std::map<std::string, bool> Vals; ///< Valuation of atomic propositions
    typedef std::pair<std::string, bool> Val; ///< A single proposition valuation

    // @return	true iff the formula is true under the valuation (where the valuation are pairs (variable,value))
    static bool resolve(const std::string & source, const Vals & valuation,  ErrorReport* errorReport = nullptr);

    // @return  new string made from the source by removing whitespaces
    static std::string removeWhitespaces(const std::string & source);

    // @return new string made from the source by removing removal all character that match the given character
    static std::string removeCharacter(const std::string &source, const char ch);

private:
    static std::vector<std::string> singleParse(const std::string & formula, const char op, ErrorReport* errorReport = nullptr);

    // @return	true iff ch is possibly part of a valid name
    static bool belongsToName(const char ch);

    // @return	true iff the formula is true under the valuation (where the valuation are pairs (variable,value))---used internally
    static bool resolveRec(const std::string & source, const Vals & valuation, ErrorReport* errorReport = nullptr);


    // @return	new string made from the source by removing the leading and trailing white spaces
    static std::string trim(const std::string & source);
};

#endif
