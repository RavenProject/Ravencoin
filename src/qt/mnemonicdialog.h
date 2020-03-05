// Copyright (c) 2017-2018 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_QT_MNEMONICDIALOG_H
#define PARTICL_QT_MNEMONICDIALOG_H

#include <QDialog>

namespace Ui {
    class MnemonicDialog;
}

class MnemonicDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MnemonicDialog(QWidget *parent);
    ~MnemonicDialog();

    void GenerateWords();
    bool eventFilter(QObject *obj, QEvent *ev);

public Q_SLOTS:
    void on_btnCancel_clicked();
    void on_btnImport_clicked();
    void on_btnGenerate_clicked();

private:
    Ui::MnemonicDialog *ui;
};

#endif // PARTICL_QT_MNEMONICDIALOG_H
