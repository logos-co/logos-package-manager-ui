#include "PackagesPagingProxy.h"

#include <algorithm>
#include <QAbstractItemModel>

PackagesPagingProxy::PackagesPagingProxy(QObject* parent)
    : QAbstractProxyModel(parent)
{
}

void PackagesPagingProxy::setSourceModel(QAbstractItemModel* sourceModel)
{
    if (this->sourceModel() == sourceModel) return;

    beginResetModel();

    if (this->sourceModel()) {
        disconnect(this->sourceModel(), nullptr, this, nullptr);
    }
    QAbstractProxyModel::setSourceModel(sourceModel);

    if (sourceModel) {
        connect(sourceModel, &QAbstractItemModel::modelReset,
                this, &PackagesPagingProxy::onSourceReset);
        connect(sourceModel, &QAbstractItemModel::rowsInserted,
                this, &PackagesPagingProxy::onSourceReset);
        connect(sourceModel, &QAbstractItemModel::rowsRemoved,
                this, &PackagesPagingProxy::onSourceReset);
        connect(sourceModel, &QAbstractItemModel::rowsMoved,
                this, &PackagesPagingProxy::onSourceReset);
        connect(sourceModel, &QAbstractItemModel::layoutChanged,
                this, &PackagesPagingProxy::onSourceReset);

        // dataChanged is forwarded selectively for cells that fall in
        // the current page's slice — that's a real signal, not a
        // structural change, so we don't reset.
        connect(sourceModel, &QAbstractItemModel::dataChanged,
                this, &PackagesPagingProxy::onSourceDataChanged);
    }

    endResetModel();
    emit totalCountChanged();
}

void PackagesPagingProxy::onSourceReset()
{
    beginResetModel();
    const bool pageChanged = (m_currentPage != 1);
    if (pageChanged) m_currentPage = 1;
    endResetModel();
    if (pageChanged) emit currentPageChanged(m_currentPage);
    emit totalCountChanged();
}

void PackagesPagingProxy::onSourceDataChanged(const QModelIndex& topLeft,
                                              const QModelIndex& bottomRight,
                                              const QList<int>& roles)
{
    if (!sourceModel()) return;
    const int offset = pageOffset();
    const int pageEnd = offset + m_pageSize;
    const int srcStart = topLeft.row();
    const int srcEnd = bottomRight.row();

    const int sliceStart = std::max(srcStart, offset);
    const int sliceEnd   = std::min(srcEnd,   pageEnd - 1);
    if (sliceStart > sliceEnd) return; 

    const QModelIndex outTl = index(sliceStart - offset, topLeft.column());
    const QModelIndex outBr = index(sliceEnd   - offset, bottomRight.column());
    emit dataChanged(outTl, outBr, roles);
}

int PackagesPagingProxy::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !sourceModel()) return 0;
    const int total = sourceModel()->rowCount();
    const int offset = pageOffset();
    if (offset >= total) return 0;
    return std::min(m_pageSize, total - offset);
}

int PackagesPagingProxy::columnCount(const QModelIndex&) const
{
    return sourceModel() ? sourceModel()->columnCount() : 1;
}

QModelIndex PackagesPagingProxy::index(int row, int column,
                                        const QModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= rowCount() ||
        column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex PackagesPagingProxy::parent(const QModelIndex&) const
{
    return {};   // flat list
}

QModelIndex PackagesPagingProxy::mapToSource(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid() || !sourceModel()) return {};
    return sourceModel()->index(proxyIndex.row() + pageOffset(),
                                proxyIndex.column());
}

QModelIndex PackagesPagingProxy::mapFromSource(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid()) return {};
    const int row = sourceIndex.row() - pageOffset();
    if (row < 0 || row >= rowCount()) return {};
    return createIndex(row, sourceIndex.column());
}

QHash<int, QByteArray> PackagesPagingProxy::roleNames() const
{
    return sourceModel() ? sourceModel()->roleNames()
                         : QAbstractProxyModel::roleNames();
}

int PackagesPagingProxy::totalCount() const
{
    return sourceModel() ? sourceModel()->rowCount() : 0;
}

void PackagesPagingProxy::setPageSize(int size)
{
    const int v = std::max(1, size);
    if (v == m_pageSize) return;
    beginResetModel();
    m_pageSize = v;

    const int total = sourceModel() ? sourceModel()->rowCount() : 0;
    const int maxPage = std::max(1, (total + v - 1) / v);   // ceil
    const bool pageChanged = (m_currentPage > maxPage);
    if (pageChanged) m_currentPage = maxPage;

    endResetModel();
    if (pageChanged) emit currentPageChanged(m_currentPage);
}

void PackagesPagingProxy::setCurrentPage(int page)
{
    const int v = std::max(1, page);
    if (v == m_currentPage) return;
    beginResetModel();
    m_currentPage = v;
    endResetModel();
    emit currentPageChanged(m_currentPage);
}
