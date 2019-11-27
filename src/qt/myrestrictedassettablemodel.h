#include "ravenunits.h"

#include <QAbstractTableModel>
#include <QStringList>

class PlatformStyle;
class MyRestrictedAssetRecord;
class MyRestrictedAssetsTablePriv;
class WalletModel;

class CWallet;

/** UI model for the transaction table of a wallet.
 */
class MyRestrictedAssetsTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit MyRestrictedAssetsTableModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent = 0);
    ~MyRestrictedAssetsTableModel();

    enum ColumnIndex {
        Date = 0,
        Type = 1,
        ToAddress = 2,
        AssetName = 3
    };

    /** Roles to get specific information from a transaction row.
        These are independent of column.
    */
    enum RoleIndex {
        /** Type of transaction */
                TypeRole = Qt::UserRole,
        /** Date and time this transaction was created */
                DateRole,
        /** Watch-only boolean */
                WatchonlyRole,
        /** Watch-only icon */
                WatchonlyDecorationRole,
        /** Address of transaction */
                AddressRole,
        /** Label of address related to transaction */
                LabelRole,
        /** Unique identifier */
                TxIDRole,
        /** Transaction hash */
                TxHashRole,
        /** Transaction data, hex-encoded */
                TxHexRole,
        /** Whole transaction as plain text */
                TxPlainTextRole,
        /** Unprocessed icon */
                RawDecorationRole,
        /** RVN or name of an asset */
                AssetNameRole,
    };

    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
    bool processingQueuedTransactions() const { return fProcessingQueuedTransactions; }

private:
    CWallet* wallet;
    WalletModel *walletModel;
    QStringList columns;
    MyRestrictedAssetsTablePriv *priv;
    bool fProcessingQueuedTransactions;
    const PlatformStyle *platformStyle;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

    QString lookupAddress(const std::string &address, bool tooltip) const;
    QVariant addressColor(const MyRestrictedAssetRecord *wtx) const;
    QString formatTxDate(const MyRestrictedAssetRecord *wtx) const;
    QString formatTxType(const MyRestrictedAssetRecord *wtx) const;
    QString formatTxToAddress(const MyRestrictedAssetRecord *wtx, bool tooltip) const;
    QString formatTooltip(const MyRestrictedAssetRecord *rec) const;
    QVariant txStatusDecoration(const MyRestrictedAssetRecord *wtx) const;
    QVariant txWatchonlyDecoration(const MyRestrictedAssetRecord *wtx) const;
    QVariant txAddressDecoration(const MyRestrictedAssetRecord *wtx) const;

public Q_SLOTS:
    void updateMyRestrictedAssets(const QString &address, const QString& asset_name, const int type, const qint64 date);
            /* New transaction, or transaction changed status */

    /** Updates the column title to "Amount (DisplayUnit)" and emits headerDataChanged() signal for table headers to react. */
    /* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
    void setProcessingQueuedTransactions(bool value) { fProcessingQueuedTransactions = value; }

    friend class MyRestrictedAssetsTablePriv;
};


