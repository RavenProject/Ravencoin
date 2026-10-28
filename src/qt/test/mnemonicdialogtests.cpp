// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/raven-config.h"
#endif

#include <qt/mnemonicdialog.h>
#include <wallet/bip39.h>
#include <wallet/wallet.h>
#include <support/cleanse.h>
#include <random.h>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
#if defined(QT_QPA_PLATFORM_MINIMAL)
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin);
#endif
#if defined(QT_QPA_PLATFORM_XCB)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin);
#endif
#endif

namespace {

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #expression); \
        return false; \
    } \
} while (false)

struct PendingInput
{
    SecureString words;
    SecureString passphrase;

    ~PendingInput()
    {
        ClearSecureString(words);
        ClearSecureString(passphrase);
        ClearPendingMnemonicInput();
    }
};

bool SelectWalletType(MnemonicDialog& dialog, const char* radioName, int pageIndex)
{
    MnemonicDialog1* page = dialog.findChild<MnemonicDialog1*>();
    CHECK(page != nullptr);
    QRadioButton* radio = page->findChild<QRadioButton*>(radioName);
    QPushButton* accept = page->findChild<QPushButton*>("acceptButton");
    QStackedLayout* layout = dialog.findChild<QStackedLayout*>();
    CHECK(radio != nullptr);
    CHECK(accept != nullptr);
    CHECK(layout != nullptr);
    radio->click();
    accept->click();
    CHECK(layout->currentIndex() == pageIndex);
    return true;
}

bool ConstructorClearsStaleInput()
{
    PendingInput cleanup;
    SetPendingMnemonicInput(SecureString("stale words"), SecureString("stale extension"));
    CHECK(HasPendingMnemonicInput());
    MnemonicDialog dialog;
    CHECK(!HasPendingMnemonicInput());
    return true;
}

bool RestoreInvalidAndBack()
{
    PendingInput cleanup;
    MnemonicDialog dialog;
    CHECK(SelectWalletType(dialog, "walletOldRadio", 2));
    MnemonicDialog3* page = dialog.findChild<MnemonicDialog3*>();
    CHECK(page != nullptr);
    QPlainTextEdit* words = page->findChild<QPlainTextEdit*>("seedwordsEdit");
    QLineEdit* passphrase = page->findChild<QLineEdit*>("passphraseEdit");
    QLabel* error = page->findChild<QLabel*>("lblHelp");
    QPushButton* accept = page->findChild<QPushButton*>("acceptButton");
    QPushButton* back = page->findChild<QPushButton*>("backButton");
    QStackedLayout* layout = dialog.findChild<QStackedLayout*>();
    CHECK(words != nullptr);
    CHECK(passphrase != nullptr);
    CHECK(error != nullptr);
    CHECK(accept != nullptr);
    CHECK(back != nullptr);
    CHECK(layout != nullptr);

    words->setPlainText(QStringLiteral("not a valid mnemonic"));
    passphrase->setText(QStringLiteral("test extension"));
    accept->click();
    CHECK(!error->text().isEmpty());
    CHECK(layout->currentIndex() == 2);
    CHECK(!HasPendingMnemonicInput());

    back->click();
    CHECK(layout->currentIndex() == 0);
    CHECK(words->toPlainText().isEmpty());
    CHECK(passphrase->text().isEmpty());
    CHECK(!HasPendingMnemonicInput());
    return true;
}

bool RestoreValidHandoff()
{
    PendingInput input;
    const QString expectedWords = QStringLiteral(
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about");
    const QString expectedPassphrase = QStringLiteral("café");
    MnemonicDialog dialog;
    CHECK(SelectWalletType(dialog, "walletOldRadio", 2));
    MnemonicDialog3* page = dialog.findChild<MnemonicDialog3*>();
    CHECK(page != nullptr);
    QPlainTextEdit* words = page->findChild<QPlainTextEdit*>("seedwordsEdit");
    QLineEdit* passphrase = page->findChild<QLineEdit*>("passphraseEdit");
    QPushButton* accept = page->findChild<QPushButton*>("acceptButton");
    CHECK(words != nullptr);
    CHECK(passphrase != nullptr);
    CHECK(accept != nullptr);

    words->setPlainText(expectedWords);
    passphrase->setText(expectedPassphrase);
    QTimer::singleShot(0, accept, [accept] { accept->click(); });
    QTimer::singleShot(5000, &dialog, [&dialog] { dialog.reject(); });
    dialog.exec();
    CHECK(HasPendingMnemonicInput());
    CHECK(words->toPlainText().isEmpty());
    CHECK(passphrase->text().isEmpty());
    CHECK(TakePendingMnemonicInput(input.words, input.passphrase));
    CHECK(QString::fromUtf8(input.words.data(), static_cast<int>(input.words.size())) == expectedWords);
    CHECK(QString::fromUtf8(input.passphrase.data(), static_cast<int>(input.passphrase.size())) == expectedPassphrase);
    CHECK(!HasPendingMnemonicInput());
    PendingInput second;
    CHECK(!TakePendingMnemonicInput(second.words, second.passphrase));
    return true;
}

bool NewWalletGeneratedHandoff()
{
    PendingInput input;
    MnemonicDialog dialog;
    CHECK(SelectWalletType(dialog, "walletNewRadio", 1));
    MnemonicDialog2* page = dialog.findChild<MnemonicDialog2*>();
    CHECK(page != nullptr);
    QPlainTextEdit* words = page->findChild<QPlainTextEdit*>("seedwordsText");
    QLineEdit* passphrase = page->findChild<QLineEdit*>("passphraseEdit");
    QPushButton* generate = page->findChild<QPushButton*>("generateButton");
    QPushButton* accept = page->findChild<QPushButton*>("acceptButton");
    CHECK(words != nullptr);
    CHECK(passphrase != nullptr);
    CHECK(generate != nullptr);
    CHECK(accept != nullptr);

    generate->click();
    const QString generated = words->toPlainText();
    CHECK(!generated.isEmpty());
    passphrase->setText(QStringLiteral("generated extension"));
    accept->click();
    CHECK(HasPendingMnemonicInput());
    CHECK(words->toPlainText().isEmpty());
    CHECK(passphrase->text().isEmpty());
    CHECK(TakePendingMnemonicInput(input.words, input.passphrase));
    CHECK(QString::fromUtf8(input.words.data(), static_cast<int>(input.words.size())) == generated);
    CHECK(CMnemonic::Check(input.words, DEFAULT_LANG));
    CHECK(input.passphrase == SecureString("generated extension"));
    CHECK(!HasPendingMnemonicInput());
    return true;
}

bool CloseWithoutAccept()
{
    PendingInput cleanup;
    MnemonicDialog dialog;
    CHECK(SelectWalletType(dialog, "walletOldRadio", 2));
    MnemonicDialog3* page = dialog.findChild<MnemonicDialog3*>();
    CHECK(page != nullptr);
    QPlainTextEdit* words = page->findChild<QPlainTextEdit*>("seedwordsEdit");
    QLineEdit* passphrase = page->findChild<QLineEdit*>("passphraseEdit");
    CHECK(words != nullptr);
    CHECK(passphrase != nullptr);
    words->setPlainText(QStringLiteral(
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"));
    passphrase->setText(QStringLiteral("cancelled extension"));
    QTimer::singleShot(0, &dialog, [&dialog] { dialog.close(); });
    CHECK(dialog.exec() == QDialog::Rejected);
    CHECK(!HasPendingMnemonicInput());
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    RandomInit();
#if defined(WIN32)
    _putenv_s("QT_QPA_PLATFORM", "minimal");
#else
    setenv("QT_QPA_PLATFORM", "minimal", 1);
#endif
    int qtArgc = 1;
    char appName[] = "MnemonicDialogTests";
    char* qtArgv[] = {appName, nullptr};
    QApplication app(qtArgc, qtArgv);
    std::fprintf(stderr, "Qt %s\n", QT_VERSION_STR);

    struct TestCase {
        const char* name;
        bool (*run)();
    };
    const TestCase cases[] = {
        {"constructor clears stale input", ConstructorClearsStaleInput},
        {"invalid restore and back", RestoreInvalidAndBack},
        {"valid restore handoff", RestoreValidHandoff},
        {"generated wallet handoff", NewWalletGeneratedHandoff},
        {"close without accept", CloseWithoutAccept},
    };

    int failures = 0;
    for (const TestCase& test : cases) {
        ClearPendingMnemonicInput();
        const bool passed = test.run();
        std::fprintf(stderr, "%s %s\n", passed ? "PASS" : "FAIL", test.name);
        if (!passed)
            ++failures;
    }
    ClearPendingMnemonicInput();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
