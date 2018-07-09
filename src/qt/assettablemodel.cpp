// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assettablemodel.h"
#include "assetrecord.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "walletmodel.h"

#include "core_io.h"

#include "amount.h"
#include "assets/assets.h"
#include "validation.h"

#include <QDebug>
#include <QStringList>

/* Just for testing */
    void getRandomAsset(char *name) {
        int len = rand()%28+3; //3 to 30
        std::cout << "len:" << len << std::endl;
        int i;
        for(i=0;i<=len;i++)
        {
            name[i]=65+rand()%26;
        }
        name[i]=0;
    }

    CAmount getRandomQty(){
        CAmount qty = (CAmount)rand() * (CAmount)rand();
        return(qty);
    }

    int getRandomUnits(){
        return(rand() % 9);
    }
/****************************/


class AssetTablePriv {
public:
    AssetTablePriv(AssetTableModel *_parent) :
            parent(_parent)
    {
    }

    AssetTableModel *parent;

    QList<AssetRecord> cachedAssets;

    // loads all current balances into cache
    void refreshWallet() {
        qDebug() << "AssetTablePriv::refreshWallet";
        cachedAssets.clear();
        if (passets) {
            {
                LOCK(cs_main);
                //GetMyOwnedAssets()...

                cachedAssets.append(AssetRecord("First Asset", COIN * 1000, 0));
                cachedAssets.append(AssetRecord("Second Asset", COIN * 10000, 0));
                char name[40];
                int a;
                for (a=0;a<1000;a++){
                    getRandomAsset(name);

                    cachedAssets.append(AssetRecord(name, getRandomQty(), getRandomUnits()));
                    
                }
            }
        }
    }


    int size() {
        qDebug() << "AssetTablePriv::size";
        return cachedAssets.size();
    }

    AssetRecord *index(int idx) {
        qDebug() << "AssetTablePriv::index(" << idx << ")";
        if (idx >= 0 && idx < cachedAssets.size()) {
            std::cout << "AssetTablePriv::index --> " << cachedAssets.at(idx).name << std::endl;
            std::cout << " or " << cachedAssets[idx].name << std::endl;
            return &cachedAssets[idx];
        }
        qDebug() << "AssetTablePriv::index --> 0";
        return 0;
    }

};

AssetTableModel::AssetTableModel(WalletModel *parent) :
        QAbstractTableModel(parent),
        walletModel(parent),
        priv(new AssetTablePriv(this))
{
    qDebug() << "AssetTableModel::AssetTableModel";
    columns << tr("Name") << tr("Quantity");

    priv->refreshWallet();
};

AssetTableModel::~AssetTableModel()
{
    qDebug() << "AssetTableModel::~AssetTableModel";
    delete priv;
};

void AssetTableModel::checkBalanceChanged() {
    // update cache and, if there were changes, emit some event so the screen updates..
    qDebug() << "AssetTableModel::CheckBalanceChanged";
}

int AssetTableModel::rowCount(const QModelIndex &parent) const
{
    qDebug() << "AssetTableModel::rowCount";
    Q_UNUSED(parent);
    return priv->size();
}

int AssetTableModel::columnCount(const QModelIndex &parent) const
{
    qDebug() << "AssetTableModel::columnCount";
    Q_UNUSED(parent);
    return columns.length();
}

QVariant AssetTableModel::data(const QModelIndex &index, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    qDebug() << "AssetTableModel::data(" << index << ", " << role << ")";
    Q_UNUSED(role);
    if(!index.isValid())
        return QVariant();
    AssetRecord *rec = static_cast<AssetRecord*>(index.internalPointer());

    switch (index.column())
    {
        case Name:
            return QString::fromStdString(rec->name);
        case Quantity:
            return QString::fromStdString(rec->formatted());
        default:
            return QString();
    }
}

QVariant AssetTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    qDebug() << "AssetTableModel::headerData";
    if(role == Qt::DisplayRole)
        {
            if (section < columns.size())
                return columns.at(section);
        }

    return QVariant();
}

QModelIndex AssetTableModel::index(int row, int column, const QModelIndex &parent) const
{
    qDebug() << "AssetTableModel::index(" << row << ", " << column << ", " << parent << ")";
    Q_UNUSED(parent);
    AssetRecord *data = priv->index(row);
    if(data)
    {
        QModelIndex idx = createIndex(row, column, priv->index(row));
        qDebug() << "AssetTableModel::index --> " << idx;
        return idx;
    }
    qDebug() << "AssetTableModel::index --> " << QModelIndex();
    return QModelIndex();
}
