// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef YOTTAFLUX_QT_YOTTAFLUXADDRESSVALIDATOR_H
#define YOTTAFLUX_QT_YOTTAFLUXADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class YottafluxAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit YottafluxAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Yottaflux address widget validator, checks for a valid yottaflux address.
 */
class YottafluxAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit YottafluxAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // YOTTAFLUX_QT_YOTTAFLUXADDRESSVALIDATOR_H
