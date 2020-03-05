// Copyright (c) 2017-2018 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/mnemonicdialog.h>
#include <qt/forms/ui_mnemonicdialog.h>

#include <qt/guiutil.h>
#include <wallet/wallet.h>

#include <QDebug>

MnemonicDialog::MnemonicDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MnemonicDialog)
{
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    ui->setupUi(this);

    QObject::connect(ui->btnCancel, &QPushButton::clicked, this, &MnemonicDialog::on_btnCancel_clicked);
    QObject::connect(ui->btnGenerate, &QPushButton::clicked, this, &MnemonicDialog::on_btnGenerate_clicked);

    ui->tbxMnemonic->installEventFilter(this);

    setWindowTitle(QString("HD Wallet Setup"));
    ui->edtPassword->setPlaceholderText(tr("Enter a passphrase to protect your Recovery Phrase. (optional)"));
#if QT_VERSION >= 0x050200
    ui->tbxMnemonic->setPlaceholderText(tr("Enter your BIP39 compliant Recovery Phrase/Mnemonic."));
#endif
};

bool MnemonicDialog::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == ui->tbxMnemonic && ev->type() == QEvent::FocusIn)
    {
        // Clear invalid flag on focus
        ui->lblHelp->clear();
    }
    return QWidget::eventFilter(obj, ev);
}

MnemonicDialog::~MnemonicDialog()
{
    delete ui;
};

void MnemonicDialog::on_btnCancel_clicked()
{
    auto btnRetVal = QMessageBox::question(this, windowTitle(),
                         tr("If you cancel, the word list will be generated for you. Are you sure you want to continue?"),
                         QMessageBox::No, QMessageBox::Yes);

    if(btnRetVal == QMessageBox::Yes)
        close();
};

void MnemonicDialog::on_btnImport_clicked()
{
    std::string words = ui->tbxMnemonic->toPlainText().toStdString();
    std::string passphrase = ui->edtPassword->text().toStdString();

    my_words = words;
    my_passphrase = passphrase;

    SecureString tmp(my_words.begin(), my_words.end());

    // NOTE: default mnemonic passphrase is an empty string
    if (!CMnemonic::Check(tmp)) {
        ui->lblHelp->setText("Words are not valid, please check the words and try again");
        my_words.clear();
        my_passphrase.clear();
        return;
    }

    close();
};

void MnemonicDialog::GenerateWords()
{
    SecureString words = CMnemonic::Generate(128);
    std::string str_words = std::string(words.begin(), words.end());
#if QT_VERSION >= 0x050200
    ui->tbxMnemonic->setText(QString::fromStdString(str_words));
#endif
}

void MnemonicDialog::on_btnGenerate_clicked()
{
   GenerateWords();
};


