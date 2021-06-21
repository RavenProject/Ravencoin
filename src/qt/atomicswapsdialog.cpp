// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "atomicswapsdialog.h"
#include "ui_atomicswapsdialog.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "sendcoinsdialog.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "ravenunits.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "guiconstants.h"

#include "wallet/coincontrol.h"
#include "policy/fees.h"
#include "wallet/fees.h"

#include <script/standard.h>
#include <base58.h>
#include <validation.h> // mempool and minRelayTxFee
#include <wallet/wallet.h>
#include <core_io.h>
#include <policy/policy.h>
#include "assets/assettypes.h"
#include "assettablemodel.h"

#include <QGraphicsDropShadowEffect>
#include <QModelIndex>
#include <QDebug>
#include <QMessageBox>
#include <QClipboard>
#include <QSettings>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
#define QTversionPreFiveEleven
#endif

AtomicSwapsDialog::AtomicSwapsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
        QDialog(parent, Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint | Qt::WindowMaximizeButtonHint),
        ui(new Ui::AtomicSwapsDialog),
        platformStyle(_platformStyle)
{
    ui->setupUi(this);
    setWindowTitle("Create Assets");
    connect(ui->signedPartialText, SIGNAL(textChanged()), this, SLOT(onSignedPartialChanged()));
    //connect(ui->createAssetButton, SIGNAL(clicked()), this, SLOT(onCreateAssetClicked()));
}

void AtomicSwapsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void AtomicSwapsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(),
                   _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance());

        // Setup the default values
        setUpValues();

        adjustSize();
    }
}


AtomicSwapsDialog::~AtomicSwapsDialog()
{
    delete ui;
}

/** Helper Methods */
void AtomicSwapsDialog::setUpValues()
{
    hideMessage();
    CheckFormState();
}

void AtomicSwapsDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
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

void AtomicSwapsDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0);
}

void AtomicSwapsDialog::showMessage(QString string)
{
    ui->messageLabel->setStyleSheet("color: red; font-size: 15pt;font-weight: bold;");
    ui->messageLabel->setText(string);
    ui->messageLabel->show();
}

void AtomicSwapsDialog::showValidMessage(QString string)
{
    ui->messageLabel->setStyleSheet("color: green; font-size: 15pt;font-weight: bold;");
    ui->messageLabel->setText(string);
    ui->messageLabel->show();
}

void AtomicSwapsDialog::hideMessage()
{
    ui->messageLabel->hide();
}

void AtomicSwapsDialog::disableCreateButton()
{
    ui->createAssetButton->setDisabled(true);
}

void AtomicSwapsDialog::enableCreateButton()
{
    ui->createAssetButton->setDisabled(false);
}

void AtomicSwapsDialog::CheckFormState()
{
    disableCreateButton(); // Disable the button by default
    hideMessage();
    
    QString errorMessage;
    AtomicSwapDetails partialSwap(ui->signedPartialText->toPlainText().toUtf8().constData());

    bool validSwap = AttemptParseTransaction(partialSwap, errorMessage);
     
    if (validSwap)
    {
        auto formattedProvided = RavenUnits::formatWithCustomName(partialSwap.ProvidedType, partialSwap.ProvidedQuantity, 2);
        auto formattedExpected = RavenUnits::formatWithCustomName(partialSwap.ExpectedType, partialSwap.ExpectedQuantity, 2);

        //This is from the perspective of the trade
        switch(partialSwap.Type)
        {
            case AtmoicSwapType::Buy:
            {
                ui->tradeTypeLabel->setText(tr("Buy Order (You are selling assets)"));
                ui->assetNameLabel->setText(formattedExpected);
                ui->totalPriceLabel->setText(QString("+") + formattedProvided); //Indicate we are getting money
                //Shows up as X.XXXXXX RVN/YYYYY
                auto formattedUnit = RavenUnits::formatWithCustomName(partialSwap.ProvidedType, partialSwap.UnitPrice);
                ui->unitPriceLabel->setText(QString("%1/[%2]").arg(formattedUnit).arg(partialSwap.ExpectedType));
                ui->summaryLabel->setText(tr("You are selling your %1 for %2").arg(formattedExpected).arg(formattedProvided));
                break;
            }
            case AtmoicSwapType::Sell:
            {
                ui->tradeTypeLabel->setText(tr("Sell Order (You are purchasing assets)"));
                ui->assetNameLabel->setText(formattedProvided);
                ui->totalPriceLabel->setText(formattedExpected);
                auto formattedUnit = RavenUnits::formatWithCustomName(partialSwap.ExpectedType, partialSwap.UnitPrice);
                ui->unitPriceLabel->setText(QString("%1/[%2]").arg(formattedUnit).arg(partialSwap.ProvidedType));
                ui->summaryLabel->setText(tr("You are buying %1 for %2").arg(formattedProvided).arg(formattedExpected));
                break;
            }
            case AtmoicSwapType::Trade:
            {
                ui->tradeTypeLabel->setText(tr("Trade Order (You are trading assets for assets)"));
                ui->assetNameLabel->setText(formattedProvided);
                ui->totalPriceLabel->setText(formattedExpected);
                auto formattedUnit = RavenUnits::formatWithCustomName(partialSwap.ProvidedType, partialSwap.UnitPrice);
                ui->unitPriceLabel->setText(QString("%1/[%2]").arg(formattedUnit).arg(partialSwap.ExpectedType));
                ui->summaryLabel->setText(tr("You are trading your %1 for their %2").arg(formattedExpected).arg(formattedProvided));
                break;
            }
        }

        showValidMessage("Valid!");
        enableCreateButton();
    } else {
        showMessage(errorMessage);
        disableCreateButton();
    }
}

bool AtomicSwapsDialog::AttemptParseTransaction(AtomicSwapDetails& result, QString errorMessage)
{
    if (!DecodeHexTx(result.Transaction, result.Raw, true))
    {
        errorMessage = tr("Unable to decode partial transaction");
        return false;
    }
        
    if(result.Transaction.vin.size() != 1 || result.Transaction.vout.size() != 1)
    {
        errorMessage = tr("Input contains an invalid number of inputs/outputs.");
        return false;
    }
    
    CTxIn swap_vin = result.Transaction.vin[0];
    CTxOut swap_vout = result.Transaction.vout[0];

    auto vin_script = ScriptToAsmStr(swap_vin.scriptSig, true);
    auto fIsSingleSign = vin_script.find("[SINGLE|ANYONECANPAY]") != std::string::npos;

    if(!fIsSingleSign)
    {
        errorMessage = tr("Must be signed with SINGLE|ANYONECANPAY");
        return false;
    }

    Coin vinCoin;
    if (!pcoinsTip->GetCoin(swap_vin.prevout, vinCoin))
    {
        errorMessage = tr("Unable to lookup previous transaction. It is either spent, or this transaction is for another ravencoin network.");
        return false;
    }
    
    //These are from our perspective. Do we need to send or do we receive assets.
    bool fSendAsset = swap_vout.scriptPubKey.IsAssetScript();
    bool fRecvAsset = vinCoin.IsAsset();
    
    AtmoicSwapType tradeType;
    CAmount totalPrice, unitPrice;
    std::string sendType;CAmount sendAmount;
    std::string recvType;CAmount recvAmount;
    if(fSendAsset) GetAssetInfoFromScript(swap_vout.scriptPubKey, sendType, sendAmount);
    if(fRecvAsset) GetAssetInfoFromScript(vinCoin.out.scriptPubKey, recvType, recvAmount);
    
    if(fSendAsset && fRecvAsset)
    {
        tradeType = AtmoicSwapType::Trade;
        totalPrice = sendAmount;
        unitPrice = (CAmount)((double)totalPrice / recvAmount * COIN); //Consider our input the "currency"
    }
    else if(fSendAsset)
    {
        tradeType = AtmoicSwapType::Buy;
        totalPrice = vinCoin.out.nValue;
        unitPrice = (CAmount)((double)totalPrice / sendAmount * COIN);

        recvType = "RVN";
        recvAmount = totalPrice;
    }
    else if(fRecvAsset)
    {
        tradeType = AtmoicSwapType::Sell;
        totalPrice = swap_vout.nValue;
        unitPrice = (CAmount)((double)totalPrice / recvAmount * COIN);

        sendType = "RVN";
        sendAmount = totalPrice;
    }
    else
    {
        errorMessage = tr("Unknown swap type");
        return false;
    }

    //Need to flip the perspective for the trade itself
    result.Type = tradeType;
    result.ProvidedType = QString::fromStdString(recvType);
    result.ExpectedType = QString::fromStdString(sendType);
    result.ProvidedQuantity = recvAmount;
    result.ExpectedQuantity = sendAmount;
    result.UnitPrice = unitPrice;

    return true;
}

/** SLOTS */

void AtomicSwapsDialog::onSignedPartialChanged()
{
    CheckFormState();
}

void AtomicSwapsDialog::clear()
{
    ui->signedPartialText->clear();
    hideMessage();
    disableCreateButton();
}

/** Helper functions **/
