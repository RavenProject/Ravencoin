// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "assetfilterproxy.h"
#include "assettablemodel.h"


AssetFilterProxy::AssetFilterProxy(QObject *parent) :
        QSortFilterProxyModel(parent),
        assetNamePrefix()
{
}

bool AssetFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    QString assetName = index.data(AssetTableModel::AssetNameRole).toString();

    if(!assetName.startsWith(assetNamePrefix, Qt::CaseInsensitive))
        return false;

    return true;
}

void AssetFilterProxy::setAssetNamePrefix(const QString &_assetNamePrefix)
{
    this->assetNamePrefix = _assetNamePrefix;
    invalidateFilter();
}