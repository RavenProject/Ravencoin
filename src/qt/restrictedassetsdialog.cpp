// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "restrictedassetsdialog.h"
#include "ui_restrictedassetsdialog.h"

#include "ravenunits.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "assettablemodel.h"
#include "assetfilterproxy.h"

#include "base58.h"
#include "chainparams.h"
#include "validation.h" // mempool and minRelayTxFee
#include "ui_interface.h"
#include "txmempool.h"
#include "policy/fees.h"
#include "wallet/fees.h"
#include "guiconstants.h"
#include "restrictedassignqualifier.h"
#include "ui_restrictedassignqualifier.h"
#include "restrictedfreezeaddress.h"
#include "ui_restrictedfreezeaddress.h"
#include "sendcoinsdialog.h"
#include "myrestrictedassettablemodel.h"

#include <QGraphicsDropShadowEffect>
#include <QFontMetrics>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>

#include <policy/policy.h>
#include <core_io.h>
#include <rpc/mining.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>

RestrictedAssetsDialog::RestrictedAssetsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
        QDialog(parent),
        ui(new Ui::RestrictedAssetsDialog),
        clientModel(0),
        model(0),
        platformStyle(_platformStyle)
{

    ui->setupUi(this);
    setWindowTitle("Manage Restricted Assets");
    setupStyling(_platformStyle);
}

void RestrictedAssetsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

void RestrictedAssetsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel()) {
        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(),
                   _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance());
        connect(_model, SIGNAL(balanceChanged(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)), this,
                SLOT(setBalance(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();


        assetFilterProxy = new AssetFilterProxy(this);
        assetFilterProxy->setSourceModel(_model->getAssetTableModel());
        assetFilterProxy->setDynamicSortFilter(true);
        assetFilterProxy->setAssetNamePrefix("$");
        assetFilterProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
        assetFilterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

        myRestrictedAssetsFilterProxy = new QSortFilterProxyModel(this);
        myRestrictedAssetsFilterProxy->setSourceModel(_model->getMyRestrictedAssetsTableModel());
        myRestrictedAssetsFilterProxy->setDynamicSortFilter(true);
        myRestrictedAssetsFilterProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
        myRestrictedAssetsFilterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

        myRestrictedAssetsFilterProxy->setSortRole(Qt::EditRole);

        ui->myAddressList->setModel(myRestrictedAssetsFilterProxy);
        ui->myAddressList->horizontalHeader()->setStretchLastSection(true);
        ui->myAddressList->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui->myAddressList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->myAddressList->setAlternatingRowColors(true);
        ui->myAddressList->setSortingEnabled(true);
        ui->myAddressList->verticalHeader()->hide();

        ui->listAssets->setModel(assetFilterProxy);
        ui->listAssets->horizontalHeader()->setStretchLastSection(true);
        ui->listAssets->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        ui->listAssets->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->listAssets->setAlternatingRowColors(true);
        ui->listAssets->verticalHeader()->hide();

        AssignQualifier *assignQualifier = new AssignQualifier(platformStyle, this);
        assignQualifier->setWalletModel(_model);
        assignQualifier->setObjectName("tab_assign_qualifier");
        connect(assignQualifier->getUI()->buttonSubmit, SIGNAL(clicked()), this, SLOT(assignQualifierClicked()));
        ui->tabWidget->addTab(assignQualifier, "Assign/Remove Qualifier");

        FreezeAddress *freezeAddress = new FreezeAddress(platformStyle, this);
        freezeAddress->setWalletModel(_model);
        freezeAddress->setObjectName("tab_freeze_address");
        connect(freezeAddress->getUI()->buttonSubmit, SIGNAL(clicked()), this, SLOT(freezeAddressClicked()));
        ui->tabWidget->addTab(freezeAddress, "Restrict Addresses/Global");
    }
}

RestrictedAssetsDialog::~RestrictedAssetsDialog()
{
    QSettings settings;
    delete ui;
}

void RestrictedAssetsDialog::setupStyling(const PlatformStyle *platformStyle)
{
    /** Update the restrictedassets frame */
    ui->frameAssetBalance->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));
    ui->frameAddressList->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));

    ui->tabFrame->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));

    /** Create the shadow effects on the frames */
    ui->frameAssetBalance->setGraphicsEffect(GUIUtil::getShadowEffect());
    ui->frameAddressList->setGraphicsEffect(GUIUtil::getShadowEffect());
    ui->tabFrame->setGraphicsEffect(GUIUtil::getShadowEffect());

    /** Add label color and font */
    ui->labelAssetBalance->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelAssetBalance->setFont(GUIUtil::getTopLabelFont());

    ui->labelAddressList->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelAddressList->setFont(GUIUtil::getTopLabelFont());
}



QWidget *RestrictedAssetsDialog::setupTabChain(QWidget *prev)
{
//    QWidget::setTabOrder(prev, ui->sendButton);
//    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
//    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    return prev;
}

void RestrictedAssetsDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                                 const CAmount& watchBalance, const CAmount& watchUnconfirmedBalance, const CAmount& watchImmatureBalance)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    Q_UNUSED(watchBalance);
    Q_UNUSED(watchUnconfirmedBalance);
    Q_UNUSED(watchImmatureBalance);

    ui->labelBalance->setFont(GUIUtil::getSubLabelFont());
    ui->label->setFont(GUIUtil::getSubLabelFont());

    if(model && model->getOptionsModel())
    {
        ui->labelBalance->setText(RavenUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
    }
}

void RestrictedAssetsDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0);
}

void RestrictedAssetsDialog::freezeAddressClicked()
{
    // Check wallet unlock status
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        return;
    }

    // Get the widget belonging to the freeze address tab
    FreezeAddress* widget = ui->tabWidget->findChild<FreezeAddress *>("tab_freeze_address");

    std::string asset_name = widget->getUI()->assetComboBox->currentData(AssetTableModel::RoleIndex::AssetNameRole).toString().toStdString();
    std::string address = widget->getUI()->lineEditAddress->text().toStdString();
    std::string change_address = widget->getUI()->checkBoxChangeAddress->isChecked() ? widget->getUI()->lineEditChangeAddress->text().toStdString(): "";
    std::string decodedAssetData = DecodeAssetData(widget->getUI()->lineEditAssetData->text().toStdString());

    // Get the single address options
    bool fFreezeAddress = widget->getUI()->radioButtonFreezeAddress->isChecked();
    bool fUnfreezeAddress = widget->getUI()->radioButtonUnfreezeAddress->isChecked();

    // Get the global options
    bool fFreezeGlobal = widget->getUI()->radioButtonGlobalFreeze->isChecked();
    bool fUnfreezeGlobal = widget->getUI()->radioButtonGlobalUnfreeze->isChecked();

    // Create parameters for transaction construction
    CReserveKey reservekey(model->getWallet());
    CWalletTx transaction;
    CAmount nRequiredFee;
    CCoinControl ctrl;

    // If the optional change address wasn't given create a new change address for this wallet
    if (change_address == "") {
        CKeyID keyID;
        std::string strFailReason;
        if (!model->getWallet()->CreateNewChangeAddress(reservekey, keyID, strFailReason)) {
            QMessageBox changeAddressBox;
            changeAddressBox.setText(tr("Failed to create a change address"));
            changeAddressBox.exec();
            return;
        }

        change_address = EncodeDestination(keyID);
    }

    ctrl.destChange = DecodeDestination(change_address);

    std::pair<int, std::string> error;
    std::vector< std::pair<CAssetTransfer, std::string> >vTransfers;

    // Create the pointers which is passed to the CreateTransferAssetTransaction function
    std::vector< std::pair<CNullAssetTxData, std::string> > vecFreezeAddressTxData;
    std::vector<CNullAssetTxData> vecFreezeGlobalTxData;

    // We have to send the owner token for the asset in order to perform a restriction
    std::string asset_owner_token = RestrictedNameToOwnerName(asset_name);

    vTransfers.emplace_back(std::make_pair(CAssetTransfer(asset_owner_token, 1 * COIN, decodedAssetData), change_address));

    int flag = -1;
    if (fFreezeAddress || fUnfreezeAddress) {
        flag = fFreezeAddress ? 1 : 0;
        vecFreezeAddressTxData.push_back(std::make_pair(CNullAssetTxData(asset_name, flag), address));
    } else if (fFreezeGlobal || fUnfreezeGlobal) {
        flag = fFreezeGlobal ? 1 : 0;
        vecFreezeGlobalTxData.push_back(CNullAssetTxData(asset_name, flag));
    }

    if (flag == -1) {
        QMessageBox failMsgBox;
        failMsgBox.setText(tr("Failed to generate the correct transaction. Please try again"));
        failMsgBox.exec();
        return;
    }

    if (IsInitialBlockDownload()) {
        GUIUtil::SyncWarningMessage syncWarning(this);
        bool sendTransaction = syncWarning.showTransactionSyncWarningMessage();
        if (!sendTransaction)
            return;
    }

    // Create the Transaction
    if (!CreateTransferAssetTransaction(model->getWallet(), ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, &vecFreezeAddressTxData, &vecFreezeGlobalTxData)) {
        QMessageBox createTransactionBox;
        createTransactionBox.setText(QString::fromStdString(error.second));
        createTransactionBox.exec();
        return;
    }

    QString freezingAddress = tr("Freezing all trading of the restricted asset <b>%1</b> from address <b>%2</b><br>").arg(QString::fromStdString(asset_name), QString::fromStdString(address));
    QString unfreezingAddress = tr("Unfreezing trading of the restricted asset <b>%1</b> from address <b>%2</b><br>").arg(QString::fromStdString(asset_name), QString::fromStdString(address));
    QString freezingGlobal = tr("Freezing all trading of the restricted asset <b>%1</b> from all addresses<br>").arg(QString::fromStdString(asset_name));
    QString unfreezingGlobal = tr("Opening / Unfreezing all trading of the restricted asset <b>%1</b> from all addresses<br>").arg(QString::fromStdString(asset_name));

    QString questionString;
    // Format confirmation message

    if (fFreezeAddress || fUnfreezeAddress) {
        questionString.append(flag ? freezingAddress : unfreezingAddress);
    } else if (fFreezeGlobal || fUnfreezeGlobal) {
        questionString.append(flag ? freezingGlobal : unfreezingGlobal);
    }

    if(nRequiredFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#e82121;'>");
        questionString.append(RavenUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), nRequiredFee));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));

        // append transaction size
        questionString.append(" (" + QString::number((double)GetVirtualTransactionSize(transaction) / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount =  nRequiredFee;
    QStringList alternativeUnits;
    for (RavenUnits::Unit u : RavenUnits::availableUnits())
    {
        if(u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(RavenUnits::formatHtmlWithUnit(u, totalAmount));
    }
    questionString.append(tr("Total Amount %1")
                                  .arg(RavenUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));
    questionString.append(QString("<span style='font-size:10pt;font-weight:normal;'><br />(=%2)</span>")
                                  .arg(alternativeUnits.join(" " + tr("or") + "<br />")));

    QString addString = tr("Confirm adding restriction");
    QString removingString = tr("Confirm removing resetricton");
    SendConfirmationDialog confirmationDialog(flag ? addString : removingString,
                                              questionString, SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

    if(retval != QMessageBox::Yes)
    {
        return;
    }

    // Send the Transaction to the network
    std::string txid;
    if (!SendAssetTransaction(model->getWallet(), transaction, reservekey, error, txid)) {
        QMessageBox sendTransactionBox;
        sendTransactionBox.setText(QString::fromStdString(error.second));
        sendTransactionBox.exec();
    }

    QMessageBox txidMsgBox;
    std::string sentMsg = _("Sent new transaction to the network");
    std::string totalMsg = strprintf("%s: %s",sentMsg, txid);
    txidMsgBox.setText(QString::fromStdString(totalMsg));
    txidMsgBox.exec();

    widget->clear();
}

void RestrictedAssetsDialog::assignQualifierClicked()
{
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        return;
    }

    AssignQualifier* widget = ui->tabWidget->findChild<AssignQualifier *>("tab_assign_qualifier");

    std::string address = widget->getUI()->lineEditAddress->text().toStdString();
    std::string asset_name = widget->getUI()->assetComboBox->currentData(AssetTableModel::RoleIndex::AssetNameRole).toString().toStdString();
    std::string change_address = widget->getUI()->checkBoxChangeAddress->isChecked() ? widget->getUI()->lineEditChangeAddress->text().toStdString(): "";
    std::string decodedAssetData = DecodeAssetData(widget->getUI()->lineEditAssetData->text().toStdString());

    int flag = widget->getUI()->assignTypeComboBox->currentIndex() ? 0 : 1;

    CReserveKey reservekey(model->getWallet());
    CWalletTx transaction;
    CAmount nRequiredFee;
    CCoinControl ctrl;

    // If the optional change address wasn't given create a new change address for this wallet
    if (change_address == "") {
        CKeyID keyID;
        std::string strFailReason;
        if (!model->getWallet()->CreateNewChangeAddress(reservekey, keyID, strFailReason)) {
            QMessageBox changeAddressBox;
            changeAddressBox.setText(tr("Failed to create a change address"));
            changeAddressBox.exec();
            return;
        }

        change_address = EncodeDestination(keyID);
    }

    ctrl.destChange = DecodeDestination(change_address);

    std::pair<int, std::string> error;
    std::vector< std::pair<CAssetTransfer, std::string> >vTransfers;

    // Always transfer 1 of the qualifier tokens to the change address
    vTransfers.emplace_back(std::make_pair(CAssetTransfer(asset_name, 1 * COIN, decodedAssetData), change_address));

    // Add the asset data with the flag to remove or add the tag 1 = Add, 0 = Remove
    std::vector< std::pair<CNullAssetTxData, std::string> > vecAssetData;
    vecAssetData.push_back(std::make_pair(CNullAssetTxData(asset_name, flag), address));

    // Create the Transaction
    if (!CreateTransferAssetTransaction(model->getWallet(), ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, &vecAssetData)) {
        QMessageBox createTransactionBox;
        createTransactionBox.setText(QString::fromStdString(error.second));
        createTransactionBox.exec();
        return;
    }

    QString addingQualifier = tr("Adding qualifier <b>%1</b> to address <b>%2</b><br>").arg(QString::fromStdString(asset_name), QString::fromStdString(address));
    QString removingQualifier = tr("Removing qualifier <b>%1</b> from address <b>%2</b><br>").arg(QString::fromStdString(asset_name), QString::fromStdString(address));

    QString questionString;
    // Format confirmation message

    questionString.append(flag ? addingQualifier : removingQualifier);
    if(nRequiredFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#e82121;'>");
        questionString.append(RavenUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), nRequiredFee));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));

        // append transaction size
        questionString.append(" (" + QString::number((double)GetVirtualTransactionSize(transaction) / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount =  nRequiredFee;
    QStringList alternativeUnits;
    for (RavenUnits::Unit u : RavenUnits::availableUnits())
    {
        if(u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(RavenUnits::formatHtmlWithUnit(u, totalAmount));
    }
    questionString.append(tr("Total Amount %1")
                                  .arg(RavenUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));
    questionString.append(QString("<span style='font-size:10pt;font-weight:normal;'><br />(=%2)</span>")
                                  .arg(alternativeUnits.join(" " + tr("or") + "<br />")));

    QString addString = tr("Confirm adding qualifier");
    QString removingString = tr("Confirm removing qualifier");
    SendConfirmationDialog confirmationDialog(flag ? addString : removingString,
                                              questionString, SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

    if(retval != QMessageBox::Yes)
    {
        return;
    }

    // Send the Transaction to the network
    std::string txid;
    if (!SendAssetTransaction(model->getWallet(), transaction, reservekey, error, txid)) {
        QMessageBox sendTransactionBox;
        sendTransactionBox.setText(QString::fromStdString(error.second));
        sendTransactionBox.exec();
    }

    QMessageBox txidMsgBox;
    std::string sentMsg = _("Sent new transaction to the network");
    std::string totalMsg = strprintf("%s: %s",sentMsg, txid);
    txidMsgBox.setText(QString::fromStdString(totalMsg));
    txidMsgBox.exec();

    widget->clear();
}






