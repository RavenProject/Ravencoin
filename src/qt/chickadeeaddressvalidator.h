// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2017 The Chickadee Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CHICKADEE_QT_CHICKADEEADDRESSVALIDATOR_H
#define CHICKADEE_QT_CHICKADEEADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class ChickadeeAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit ChickadeeAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Chickadee address widget validator, checks for a valid chickadee address.
 */
class ChickadeeAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit ChickadeeAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // CHICKADEE_QT_CHICKADEEADDRESSVALIDATOR_H
