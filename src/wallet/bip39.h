/**
 * Created by ROSHii on 2019-06-01.
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRC_BIP39_H
#define SRC_BIP39_H

#include "support/allocators/secure.h"

const unsigned int NUM_LANGUAGES_BIP39_SUPPORTED = 8;

struct LanguageDetails
{
    const char* Label;
    const char* name;
    const int minimumWordsCheckLang;
    const char * const* wordlist;
};


class CMnemonic
{
public:
    static SecureString Generate(int strength, int languageSelected = 0);    // strength in bits
    static SecureString FromData(const SecureVector& data, int len, int languageSelected = 0);
    static bool Check(SecureString mnemonic, int languageSelected = -1);
    static int DetectLanguageSeed(SecureString mnemonic);
    static std::array<LanguageDetails, 8> GetLanguagesDetails();
    static const char * const* GetLanguageWords(int lang);
    static void ToSeed(SecureString mnemonic, SecureString passphrase, SecureVector& seedRet);
private:
    CMnemonic() {};
};

#endif