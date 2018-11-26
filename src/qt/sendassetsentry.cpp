// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "sendassetsentry.h"
#include "ui_sendassetsentry.h"
//#include "sendcoinsentry.h"
//#include "ui_sendcoinsentry.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "assetcontroldialog.h"
#include "guiconstants.h"

#include "wallet/coincontrol.h"

#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QClipboard>
#include <validation.h>
#include <core_io.h>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>

SendAssetsEntry::SendAssetsEntry(const PlatformStyle *_platformStyle, const QStringList myAssetsNames, QWidget *parent) :
    QStackedWidget(parent),
    ui(new Ui::SendAssetsEntry),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    ui->addressBookButton->setIcon(platformStyle->SingleColorIcon(":/icons/address-book"));
    ui->pasteButton->setIcon(platformStyle->SingleColorIcon(":/icons/editpaste"));
    ui->deleteButton->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
    ui->deleteButton_is->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
    ui->deleteButton_s->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));

    setCurrentWidget(ui->SendCoins);

    if (platformStyle->getUseExtraSpacing())
        ui->payToLayout->setSpacing(4);
#if QT_VERSION >= 0x040700
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
#endif

    // normal raven address field
    GUIUtil::setupAddressWidget(ui->payTo, this);
    // just a label for displaying raven address(es)
    ui->payTo_is->setFont(GUIUtil::fixedPitchFont());

    // Connect signals
    connect(ui->payAssetAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->assetSelectionBox, SIGNAL(activated(int)), this, SLOT(onAssetSelected(int)));
    connect(ui->administratorCheckbox, SIGNAL(clicked()), this, SLOT(onSendOwnershipChanged()));

    ui->administratorCheckbox->setToolTip(tr("Select to view administrator assets to transfer"));

    /** Setup the asset list combobox */
    stringModel = new QStringListModel;
    stringModel->insertRow(stringModel->rowCount());
    stringModel->setData(stringModel->index(stringModel->rowCount() - 1, 0), "", Qt::DisplayRole);

    for (auto name : myAssetsNames)
    {
        stringModel->insertRow(stringModel->rowCount());
        stringModel->setData(stringModel->index(stringModel->rowCount() - 1, 0), name, Qt::DisplayRole);
    }

    proxy = new QSortFilterProxyModel;
    proxy->setSourceModel(stringModel);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->assetSelectionBox->setModel(proxy);
    ui->assetSelectionBox->setEditable(true);

    completer = new QCompleter(proxy,this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->assetSelectionBox->setCompleter(completer);

    ui->assetSelectionBox->lineEdit()->setPlaceholderText(tr("Select an asset to transfer"));
    ui->assetSelectionBox->setMinimumWidth(32);

    /** Setup the amount box */
    ui->ownershipWarningMessage->hide();

    fShowAdministratorList = false;

    this->setStyleSheet(QString(".SendAssetsEntry {background-color: %1; padding-top: 10px; padding-right: 30px; border: none;}").arg(platformStyle->SendEntriesBackGroundColor().name()));

    this->setGraphicsEffect(GUIUtil::getShadowEffect());

    ui->assetBoxLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->assetBoxLabel->setFont(GUIUtil::getSubLabelFont());

    ui->payToLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->payToLabel->setFont(GUIUtil::getSubLabelFont());

    ui->labellLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->labellLabel->setFont(GUIUtil::getSubLabelFont());

    ui->amountLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->amountLabel->setFont(GUIUtil::getSubLabelFont());

    ui->messageLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->messageLabel->setFont(GUIUtil::getSubLabelFont());

    ui->payAssetAmount->setUnit(MAX_UNIT);
    ui->payAssetAmount->setDisabled(false);

    ui->administratorCheckbox->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->assetSelectionBox->setFont(GUIUtil::getSubLabelFont());
    ui->administratorCheckbox->setFont(GUIUtil::getSubLabelFont());
    ui->payTo->setFont(GUIUtil::getSubLabelFont());
    ui->addAsLabel->setFont(GUIUtil::getSubLabelFont());
    ui->payAssetAmount->setFont(GUIUtil::getSubLabelFont());
    ui->messageTextLabel->setFont(GUIUtil::getSubLabelFont());
    ui->assetAmountLabel->setFont(GUIUtil::getSubLabelFont());
    ui->ownershipWarningMessage->setFont(GUIUtil::getSubLabelFont());
}

SendAssetsEntry::~SendAssetsEntry()
{
    delete ui;
}

void SendAssetsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendAssetsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAssetAmount->setFocus();
    }
}

void SendAssetsEntry::on_payTo_textChanged(const QString &address)
{
    updateLabel(address);
}

void SendAssetsEntry::setModel(WalletModel *_model)
{
    this->model = _model;

//    if (_model && _model->getOptionsModel())
//        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    clear();
}

void SendAssetsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->messageTextLabel->clear();
    ui->messageTextLabel->hide();
    ui->messageLabel->hide();
    // clear UI elements for unauthenticated payment request
    ui->memoTextLabel_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();

    ui->payAssetAmount->clear();

    // Reset the selected asset
    ui->assetSelectionBox->setCurrentIndex(0);
}

void SendAssetsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

bool SendAssetsEntry::validate()
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;

    if (!model->validateAddress(ui->payTo->text()))
    {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (ui->assetSelectionBox->currentIndex() == 0) {
        ui->assetSelectionBox->lineEdit()->setStyleSheet(STYLE_INVALID);
        retval = false;
    }

    if (!ui->payAssetAmount->validate())
    {
        retval = false;
    }

    if (ui->payAssetAmount->value(0) <= 0)
    {
        ui->payAssetAmount->setValid(false);
        retval = false;
    }

    // TODO check to make sure the payAmount value is within the constraints of how much you own

    return retval;
}

SendAssetsRecipient SendAssetsEntry::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    // Normal payment
    recipient.assetName = ui->assetSelectionBox->currentText();
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    recipient.amount = ui->payAssetAmount->value();
    recipient.message = ui->messageTextLabel->text();

    return recipient;
}

QWidget *SendAssetsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
}

void SendAssetsEntry::setValue(const SendAssetsRecipient &value)
{
    recipient = value;

    if (recipient.assetName != "") {
        int index = ui->assetSelectionBox->findText(recipient.assetName);
        ui->assetSelectionBox->setCurrentIndex(index);
        onAssetSelected(index);
    }
}

void SendAssetsEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAssetAmount->setFocus();
}

bool SendAssetsEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendAssetsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendAssetsEntry::setFocusAssetListBox()
{
    ui->assetSelectionBox->setFocus();
}

bool SendAssetsEntry::updateLabel(const QString &address)
{
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}

void SendAssetsEntry::onAssetSelected(int index)
{
    ui->assetSelectionBox->lineEdit()->setStyleSheet("");
    QString name = ui->assetSelectionBox->currentText();
    // If the name
    if (index == 0) {
        ui->assetAmountLabel->clear();
        if(!ui->administratorCheckbox->isChecked())
            ui->payAssetAmount->setDisabled(false);
        ui->payAssetAmount->clear();
        return;
    }

    // Check to see if the asset selected is an ownership asset
    bool fIsOwnerAsset = false;
    if (IsAssetNameAnOwner(name.toStdString())) {
        fIsOwnerAsset = true;
        name = name.split("!").first();
    }

    LOCK(cs_main);
    CNewAsset asset;

    // Get the asset metadata if it exists. This isn't called on the administrator token because that doesn't have metadata
    if (!passets->GetAssetMetaDataIfExists(name.toStdString(), asset)) {
        // This should only happen if the user, selected an asset that was issued from assetcontrol and tries to transfer it before it is mined.
        clear();
        ui->messageLabel->show();
        ui->messageTextLabel->show();
        ui->messageTextLabel->setText(tr("Failed to get asset metadata for: ") + name + "." + tr(" The transaction in which the asset was issued must be mined into a block before you can transfer it"));
        ui->assetAmountLabel->clear();
        return;
    }

    CAmount amount = 0;

    if(!model || !model->getWallet())
        return;

    std::map<std::string, std::vector<COutput> > mapAssets;
    model->getWallet()->AvailableAssets(mapAssets, true, AssetControlDialog::assetControl);

    // Add back the OWNER_TAG (!) that was removed above
    if (fIsOwnerAsset)
        name = name + OWNER_TAG;


    if (!mapAssets.count(name.toStdString())) {
        clear();
        ui->messageLabel->show();
        ui->messageTextLabel->show();
        ui->messageTextLabel->setText(tr("Failed to get asset outpoints from database"));
        return;
    }

    auto vec = mapAssets.at(name.toStdString());

    // Go through all of the mapAssets to get the total count of assets
    for (auto txout : vec) {
        CAssetOutputEntry data;
        if (GetAssetData(txout.tx->tx->vout[txout.i].scriptPubKey, data))
            amount += data.nAmount;
    }

    int units = fIsOwnerAsset ? OWNER_UNITS : asset.units;

    QString displayBalance = AssetControlDialog::assetControl->HasAssetSelected() ? tr("Selected Balance") : tr("Wallet Balance");

    ui->assetAmountLabel->setText(
            displayBalance + ": <b>" + QString::fromStdString(ValueFromAmountString(amount, units)) + "</b> " + name);

    ui->messageLabel->hide();
    ui->messageTextLabel->hide();

    // If it is an ownership asset lock the amount
    if (!fIsOwnerAsset) {
        ui->payAssetAmount->setUnit(asset.units);
        ui->payAssetAmount->setDisabled(false);
    }
}

void SendAssetsEntry::onSendOwnershipChanged()
{
    switchAdministratorList(true);
}

void SendAssetsEntry::CheckOwnerBox() {
    fUsingAssetControl = true;
    switchAdministratorList();
}

void SendAssetsEntry::IsAssetControl(bool fIsAssetControl, bool fIsOwner)
{
    if (fIsOwner) {
        CheckOwnerBox();
    }
    if (fIsAssetControl) {
        ui->administratorCheckbox->setDisabled(true);
        fUsingAssetControl = true;
    }
}

void SendAssetsEntry::setCurrentIndex(int index)
{
    if (index < ui->assetSelectionBox->count()) {
        ui->assetSelectionBox->setCurrentIndex(index);
        ui->assetSelectionBox->activated(index);
    }
}

void SendAssetsEntry::refreshAssetList()
{
    switchAdministratorList(false);
}

void SendAssetsEntry::switchAdministratorList(bool fSwitchStatus)
{
    if(!model)
        return;

    if (fSwitchStatus)
        fShowAdministratorList = !fShowAdministratorList;

    if (fShowAdministratorList) {
        ui->administratorCheckbox->setChecked(true);
        if (!AssetControlDialog::assetControl->HasAssetSelected()) {
            std::vector<std::string> names;
            GetAllAdministrativeAssets(model->getWallet(), names, 0);

            QStringList list;
            list << "";
            for (auto name: names)
                list << QString::fromStdString(name);

            stringModel->setStringList(list);

            ui->assetSelectionBox->lineEdit()->setPlaceholderText(tr("Select an administrator asset to transfer"));
            ui->assetSelectionBox->setFocus();
        } else {
            ui->payTo->setFocus();
        }

        ui->payAssetAmount->setUnit(MIN_UNIT); // Min unit because this is an administrator asset
        ui->payAssetAmount->setValue(1); // When using AssetAmountField, you must use 1 instead of 1 * COIN, because of the way that AssetAmountField uses the unit and value to display the amount
        ui->payAssetAmount->setDisabled(true);


        ui->assetAmountLabel->clear();

        ui->ownershipWarningMessage->setText(tr("Warning: Transferring administrator asset"));
        ui->ownershipWarningMessage->setStyleSheet("color: red");
        ui->ownershipWarningMessage->show();
    } else {
        ui->administratorCheckbox->setChecked(false);
        if (!AssetControlDialog::assetControl->HasAssetSelected()) {
            std::vector<std::string> names;
            GetAllMyAssets(model->getWallet(), names, 0);
            QStringList list;
            list << "";
            for (auto name : names) {
                if (!IsAssetNameAnOwner(name))
                    list << QString::fromStdString(name);
            }

            stringModel->setStringList(list);
            ui->assetSelectionBox->lineEdit()->setPlaceholderText(tr("Select an asset to transfer"));
            ui->payAssetAmount->clear();
            ui->payAssetAmount->setUnit(MAX_UNIT);
            ui->assetAmountLabel->clear();
            ui->assetSelectionBox->setFocus();
        } else {
            ui->payTo->setFocus();
        }
        ui->ownershipWarningMessage->hide();
    }
}
