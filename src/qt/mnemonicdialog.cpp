// Copyright (c) 2020 Hans Schmidt
// Copyright (c) 2017-2018 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define TEST 0

#include <qt/mnemonicdialog.h>

#include <ui_mnemonicdialog1.h>
#include <ui_mnemonicdialog2.h>
#include <ui_mnemonicdialog3.h>

#if !TEST
  #include <qt/guiutil.h>
  #include <wallet/wallet.h>
#endif

MnemonicDialog::MnemonicDialog(QWidget *parent) :
    QDialog(parent)
{
    setWindowTitle(tr("HD Wallet Setup"));

    stackedLayout = new QStackedLayout(this);

    MnemonicDialog1 *pgWalletType = new MnemonicDialog1(this);
    MnemonicDialog2 *pgNewWalletInfo = new MnemonicDialog2(this);
    MnemonicDialog3 *pgOldWalletInfo = new MnemonicDialog3(this);

    connect(pgWalletType,&MnemonicDialog1::updateMainWindowStackWidget,this,&MnemonicDialog::onChangeWindowRequested);
    connect(pgNewWalletInfo,&MnemonicDialog2::updateMainWindowStackWidget,this,&MnemonicDialog::onChangeWindowRequested);
    connect(pgOldWalletInfo,&MnemonicDialog3::updateMainWindowStackWidget,this,&MnemonicDialog::onChangeWindowRequested);

    connect(pgNewWalletInfo,&MnemonicDialog2::allCloseRequested,this,&MnemonicDialog::closeMainDialog);
    connect(pgOldWalletInfo,&MnemonicDialog3::allCloseRequested,this,&MnemonicDialog::closeMainDialog);

    stackedLayout -> addWidget(pgWalletType);
    stackedLayout -> addWidget(pgNewWalletInfo);
    stackedLayout -> addWidget(pgOldWalletInfo);
    stackedLayout -> setCurrentIndex(0);

};

void MnemonicDialog::onChangeWindowRequested(int ind)  //slot
{
    stackedLayout -> setCurrentIndex(ind);
}

void MnemonicDialog::closeMainDialog()
{
    close();
}

// #############################

MnemonicDialog1::MnemonicDialog1(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::MnemonicDialog1),
    radioselected("none")
{
    ui->setupUi(this);
};

MnemonicDialog1::~MnemonicDialog1()
{
    delete ui;
};

void MnemonicDialog1::on_walletNewRadio_clicked()
{
    ui->walletLabel->setText(tr("You are choosing to create a new wallet using new seed words."));
    radioselected = "new";
};

void MnemonicDialog1::on_walletOldRadio_clicked()
{
    ui->walletLabel->setText(tr("You are choosing to re-create an old wallet using seed words which you know."));
    radioselected = "old";
};

void MnemonicDialog1::on_acceptButton_clicked()
{
    if (radioselected == "new")
        Q_EMIT updateMainWindowStackWidget(1);  // "emit" is not supported on older QT revs
    else if (radioselected == "old")
        Q_EMIT updateMainWindowStackWidget(2);  // "emit" is not supported on older QT revs
};

// =========

MnemonicDialog2::MnemonicDialog2(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::MnemonicDialog2)
{
    ui->setupUi(this);

};

MnemonicDialog2::~MnemonicDialog2()
{
    delete ui;
};

void MnemonicDialog2::on_backButton_clicked()
{
    Q_EMIT updateMainWindowStackWidget(0);  // "emit" is not supported on older QT revs
};

void MnemonicDialog2::on_acceptButton_clicked()
{
    std::string words = MnemonicDialog2::ui->seedwordsText->toPlainText().toStdString();
    std::string passphrase = MnemonicDialog2::ui->passphraseEdit->text().toStdString();

#if TEST
    std::string my_words;
    std::string my_passphrase;
#endif
    my_words = words;
    my_passphrase = passphrase;

#if TEST
    // NOTE: default mnemonic passphrase is an empty string
    if (my_words != "embark lawsuit town sunny forum churn amused gate ensuure smooth valley veteran") {
#else
    SecureString tmp(my_words.begin(), my_words.end());

    // NOTE: default mnemonic passphrase is an empty string
    if (!CMnemonic::Check(tmp)) {
#endif

        MnemonicDialog2::ui->lblHelp->setText(tr("Words are not valid, please generate new words and try again"));
        my_words.clear();
        my_passphrase.clear();
        return;
    }

    Q_EMIT allCloseRequested();
};


void MnemonicDialog2::on_generateButton_clicked()
{
    MnemonicDialog2::ui->lblHelp->clear();
    GenerateWords();
};


void MnemonicDialog2::GenerateWords()
{
#if TEST
    std::string str_words = "embark lawsuit town sunny forum churn amused gate ensuure smooth valley veteran";
#else
    SecureString words = CMnemonic::Generate(128);
    std::string str_words = std::string(words.begin(), words.end());
#endif
    MnemonicDialog2::ui->seedwordsText->setPlainText(QString::fromStdString(str_words));

}

// =========

MnemonicDialog3::MnemonicDialog3(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::MnemonicDialog3)
{
    ui->setupUi(this);

    MnemonicDialog3::ui->seedwordsEdit->installEventFilter(this);
};

bool MnemonicDialog3::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == MnemonicDialog3::ui->seedwordsEdit && ev->type() == QEvent::FocusIn)
    {
        // Clear invalid flag on focus
        MnemonicDialog3::ui->lblHelp->clear();
    }
    return QWidget::eventFilter(obj, ev);
}


MnemonicDialog3::~MnemonicDialog3()
{
    delete ui;
};

void MnemonicDialog3::on_backButton_clicked()
{
    Q_EMIT updateMainWindowStackWidget(0);  // "emit" is not supported on older QT revsöU
};

void MnemonicDialog3::on_acceptButton_clicked()
{
    std::string words = MnemonicDialog3::ui->seedwordsEdit->toPlainText().toStdString();
    std::string passphrase = MnemonicDialog3::ui->passphraseEdit->text().toStdString();

#if TEST
    std::string my_words;
    std::string my_passphrase;
#endif
    my_words = words;
    my_passphrase = passphrase;

#if TEST
    // NOTE: default mnemonic passphrase is an empty string
    if (my_words != "embark lawsuit town sunny forum churn amused gate ensuure smooth valley veteran") {
#else
    SecureString tmp(my_words.begin(), my_words.end());

    // NOTE: default mnemonic passphrase is an empty string
    if (!CMnemonic::Check(tmp)) {
#endif

        MnemonicDialog3::ui->lblHelp->setText(tr("Words are not valid, please check the words and try again"));
        my_words.clear();
        my_passphrase.clear();
        return;
    }

    Q_EMIT allCloseRequested();
};

