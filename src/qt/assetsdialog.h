// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_QT_ASSETSDIALOG_H
#define RAVEN_QT_ASSETSDIALOG_H

#include "walletmodel.h"

#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

class ClientModel;
class PlatformStyle;
class SendAssetsEntry;
class SendCoinsRecipient;

namespace Ui {
    class AssetsDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending ravens */
class AssetsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AssetsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~AssetsDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);
    void setupAssetControlFrame(const PlatformStyle *platformStyle);
    void setupScrollView(const PlatformStyle *platformStyle);
    void setupFeeControl(const PlatformStyle *platformStyle);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setAddress(const QString &address);
    void pasteEntry(const SendAssetsRecipient &rv);
    bool handlePaymentRequest(const SendAssetsRecipient &recipient);
    void processNewTransaction();

    // The first time the transfer asset screen is loaded, the wallet isn't doing loading so the asset list is empty.
    // The first time the screen is navigated to, refresh the asset list
    void handleFirstSelection();

public Q_SLOTS:
    void clear();
    void reject();
    void accept();
    SendAssetsEntry *addEntry();
    void updateTabsAndLabels();
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);
    void focusAssetListBox();

private:
    Ui::AssetsDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    bool fNewRecipientAllowed;
    bool fFeeMinimized;
    const PlatformStyle *platformStyle;

    // Process WalletModel::SendCoinsReturn and generate a pair consisting
    // of a message and message flags for use in Q_EMIT message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg = QString());
    void minimizeFeeSection(bool fMinimize);
    void updateFeeMinimizedLabel();
    // Update the passed in CCoinControl with state from the GUI
    void updateAssetControlState(CCoinControl& ctrl);



private Q_SLOTS:
    void on_sendButton_clicked();
    void on_buttonChooseFee_clicked();
    void on_buttonMinimizeFee_clicked();
    void removeEntry(SendAssetsEntry* entry);
    void updateDisplayUnit();
    void assetControlFeatureChanged(bool);
    void assetControlButtonClicked();
    void assetControlChangeChecked(int);
    void assetControlChangeEdited(const QString &);
    void assetControlUpdateLabels();
    void assetControlClipboardQuantity();
    void assetControlClipboardAmount();
    void assetControlClipboardFee();
    void assetControlClipboardAfterFee();
    void assetControlClipboardBytes();
    void assetControlClipboardLowOutput();
    void assetControlClipboardChange();
    void setMinimumFee();
    void updateFeeSectionControls();
    void updateMinFeeLabel();
    void updateSmartFeeLabel();

    void customFeeFeatureChanged(bool);

    /** RVN START */
    void assetControlUpdateSendCoinsDialog();
    void focusAsset(const QModelIndex& index);
    /** RVN END */

    Q_SIGNALS:
            // Fired when a message should be reported to the user
            void message(const QString &title, const QString &message, unsigned int style);
};

#endif // RAVEN_QT_ASSETSSDIALOG_H
