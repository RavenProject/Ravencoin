// Copyright (c) 2020 Hans Schmidt
// Copyright (c) 2017-2018 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define TEST 0

#include <qt/mnemonicdialog.h>

#include <ui_mnemonicdialog1.h>
#include <ui_mnemonicdialog2.h>
#include <ui_mnemonicdialog3.h>
#include <wallet/bip39.h>
#include <support/cleanse.h>

#include <QByteArray>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextDocument>

#include <algorithm>
#include <utility>

#if !TEST
  #include <qt/guiutil.h>
  #include <wallet/wallet.h>
#endif

namespace {

class ScopedSecureStringCleanser
{
private:
    SecureString& value;

public:
    explicit ScopedSecureStringCleanser(SecureString& valueIn) : value(valueIn) {}
    ~ScopedSecureStringCleanser() { ClearSecureString(value); }
};

class ScopedQStringCleanser
{
private:
    QString& value;

public:
    explicit ScopedQStringCleanser(QString& valueIn) : value(valueIn) {}
    ~ScopedQStringCleanser()
    {
        if (!value.isEmpty())
            memory_cleanse(value.data(), value.size() * sizeof(QChar));
    }
};

class ScopedQByteArrayCleanser
{
private:
    QByteArray& value;

public:
    explicit ScopedQByteArrayCleanser(QByteArray& valueIn) : value(valueIn) {}
    ~ScopedQByteArrayCleanser()
    {
        if (!value.isEmpty())
            memory_cleanse(value.data(), value.size());
    }
};

void ToSecureUtf8(QString text, SecureString& result)
{
    ScopedQStringCleanser cleanseText(text);
    QByteArray utf8 = text.toUtf8();
    ScopedQByteArrayCleanser cleanseUtf8(utf8);
    const size_t reserveSize = utf8.size() > 64 ? utf8.size() : 64;
    result.reserve(reserveSize);
    result.assign(utf8.constData(), utf8.constData() + utf8.size());
}

void BestEffortClear(QPlainTextEdit* edit)
{
    const int length = std::max(0, edit->document()->characterCount() - 1);
    edit->setPlainText(QString(length, QChar(' ')));
    edit->clear();
}

void BestEffortClear(QLineEdit* edit)
{
    const int length = edit->text().size();
    edit->setText(QString(length, QChar(' ')));
    edit->clear();
}

} // namespace

MnemonicDialog::MnemonicDialog(QWidget *parent) :
    QDialog(parent)
{
#if !TEST
    ClearPendingMnemonicInput();
#endif
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
    ui->seedwordsText->setUndoRedoEnabled(false);
    
    std::array<LanguageDetails, NUM_LANGUAGES_BIP39_SUPPORTED> languagesDetails = CMnemonic::GetLanguagesDetails();    
   
    for(int langNum = 0; langNum < NUM_LANGUAGES_BIP39_SUPPORTED; langNum++) {
        MnemonicDialog2::ui->languageSeedWords->addItem(languagesDetails[langNum].label);
    }
    MnemonicDialog2::ui->languageSeedWords->installEventFilter(this);

};

MnemonicDialog2::~MnemonicDialog2()
{
    BestEffortClear(ui->seedwordsText);
    BestEffortClear(ui->passphraseEdit);
    delete ui;
};

void MnemonicDialog2::on_backButton_clicked()
{
    BestEffortClear(MnemonicDialog2::ui->seedwordsText);
    BestEffortClear(MnemonicDialog2::ui->passphraseEdit);
    Q_EMIT updateMainWindowStackWidget(0);  // "emit" is not supported on older QT revs
};

void MnemonicDialog2::on_acceptButton_clicked()
{
    SecureString words;
    SecureString passphrase;
    ScopedSecureStringCleanser cleanseWords(words);
    ScopedSecureStringCleanser cleansePassphrase(passphrase);
    ToSecureUtf8(MnemonicDialog2::ui->seedwordsText->toPlainText(), words);
    ToSecureUtf8(MnemonicDialog2::ui->passphraseEdit->text(), passphrase);

    int languageSelected = MnemonicDialog2::ui->languageSeedWords->currentIndex();

#if TEST
    // NOTE: default mnemonic passphrase is an empty string
    if (words != "embark lawsuit town sunny forum churn amused gate ensuure smooth valley veteran") {
#else
    // NOTE: default mnemonic passphrase is an empty string
    if (!CMnemonic::Check(words, languageSelected)) {
#endif

        MnemonicDialog2::ui->lblHelp->setText(tr("Words are not valid, please generate new words and try again"));
        return;
    }

    BestEffortClear(MnemonicDialog2::ui->seedwordsText);
    BestEffortClear(MnemonicDialog2::ui->passphraseEdit);
#if !TEST
    SetPendingMnemonicInput(std::move(words), std::move(passphrase));
#endif
    Q_EMIT allCloseRequested();
};


void MnemonicDialog2::on_generateButton_clicked()
{
    MnemonicDialog2::ui->lblHelp->clear();
    int languageSelected = MnemonicDialog2::ui->languageSeedWords->currentIndex();
    GenerateWords(languageSelected);
};


void MnemonicDialog2::GenerateWords(int languageSelected)
{
#if TEST
    SecureString words = "embark lawsuit town sunny forum churn amused gate ensuure smooth valley veteran";
#else
    SecureString words = CMnemonic::Generate(128, languageSelected);
#endif
    ScopedSecureStringCleanser cleanseWords(words);
    BestEffortClear(MnemonicDialog2::ui->seedwordsText);
    MnemonicDialog2::ui->seedwordsText->setPlainText(
        QString::fromUtf8(words.data(), static_cast<int>(words.size())));
}

// =========

MnemonicDialog3::MnemonicDialog3(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::MnemonicDialog3)
{
    ui->setupUi(this);
    ui->seedwordsEdit->setUndoRedoEnabled(false);

    MnemonicDialog3::ui->seedwordsEdit->installEventFilter(this);

    std::array<LanguageDetails, NUM_LANGUAGES_BIP39_SUPPORTED> languagesDetails = CMnemonic::GetLanguagesDetails();    
   
    for(int langNum = 0; langNum < NUM_LANGUAGES_BIP39_SUPPORTED; langNum++) {
        MnemonicDialog3::ui->languageSeedWords->addItem(languagesDetails[langNum].label);
    }
    MnemonicDialog3::ui->languageSeedWords->installEventFilter(this);
};

bool MnemonicDialog3::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == MnemonicDialog3::ui->seedwordsEdit && ev->type() == QEvent::FocusIn)
    {
        // Clear invalid flag on focus
        MnemonicDialog3::ui->lblHelp->clear();
        MnemonicDialog3::ui->lblWarningJapanese->clear();
    }
    return QWidget::eventFilter(obj, ev);
}


MnemonicDialog3::~MnemonicDialog3()
{
    BestEffortClear(ui->seedwordsEdit);
    BestEffortClear(ui->passphraseEdit);
    delete ui;
};

void MnemonicDialog3::on_backButton_clicked()
{
    BestEffortClear(MnemonicDialog3::ui->seedwordsEdit);
    BestEffortClear(MnemonicDialog3::ui->passphraseEdit);
    Q_EMIT updateMainWindowStackWidget(0);  // "emit" is not supported on older QT revsöU
};

void MnemonicDialog3::on_acceptButton_clicked()
{
    SecureString words;
    SecureString passphrase;
    ScopedSecureStringCleanser cleanseWords(words);
    ScopedSecureStringCleanser cleansePassphrase(passphrase);
    ToSecureUtf8(MnemonicDialog3::ui->seedwordsEdit->toPlainText(), words);
    ToSecureUtf8(MnemonicDialog3::ui->passphraseEdit->text(), passphrase);

    int languageSelected = MnemonicDialog3::ui->languageSeedWords->currentIndex();

#if TEST
    // NOTE: default mnemonic passphrase is an empty string
    if (words != "embark lawsuit town sunny forum churn amused gate ensuure smooth valley veteran") {
#else
    // NOTE: default mnemonic passphrase is an empty string
    if (!CMnemonic::Check(words, languageSelected)) {
#endif

        MnemonicDialog3::ui->lblHelp->setText(tr("Words are not valid, please check the words and the language, and try again."));
        
        if (CMnemonic::GetLanguagesDetails()[languageSelected].name == JAPANESE){
            MnemonicDialog3::ui->lblWarningJapanese->setText(tr("In Japanese, please use standard space, ideographic japanese space is not supported."));
        }else {
            MnemonicDialog3::ui->lblWarningJapanese->clear();
        }
        return;
    }

    BestEffortClear(MnemonicDialog3::ui->seedwordsEdit);
    BestEffortClear(MnemonicDialog3::ui->passphraseEdit);
#if !TEST
    SetPendingMnemonicInput(std::move(words), std::move(passphrase));
#endif
    Q_EMIT allCloseRequested();
};
