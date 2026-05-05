#pragma once

#include <QAbstractProxyModel>
#include <QHash>

// Slicing proxy stacked on top of PackagesFilterProxy. Subclassing
// QAbstractProxyModel (not QIdentityProxyModel / QSortFilterProxyModel)
// lets us own how source signals are translated — every source mutation
// becomes a wholesale modelReset for our paged view, side-stepping the
// QSortFilterProxyModel "auto-forwarded rowsInserted with indices outside
// our paged rowCount" crash from the previous attempt.
//
// Layout:
//   PackageListModel (raw rows)
//        │
//        ▼
//   PackagesFilterProxy   (search / installState filter + sort)
//        │
//        ▼
//   PackagesPagingProxy   (this — slices to current page only)
//        │
//        ▼
//   ui-host remoting → QML via logos.model("package_manager_ui","packages")
class PackagesPagingProxy : public QAbstractProxyModel {
    Q_OBJECT

public:
    explicit PackagesPagingProxy(QObject* parent = nullptr);

    void setSourceModel(QAbstractItemModel* sourceModel) override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;

    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;

    QHash<int, QByteArray> roleNames() const override;

    int  pageSize() const { return m_pageSize; }
    void setPageSize(int size);

    int  currentPage() const { return m_currentPage; }
    void setCurrentPage(int page);

    // Total rows in the source 
    int totalCount() const;

signals:
    void totalCountChanged();
    void currentPageChanged(int page);

private slots:
    void onSourceReset();
    void onSourceDataChanged(const QModelIndex& topLeft,
                             const QModelIndex& bottomRight,
                             const QList<int>& roles);

private:
    int pageOffset() const { return (m_currentPage - 1) * m_pageSize; }

    int m_pageSize    = 20;
    int m_currentPage = 1;
};
