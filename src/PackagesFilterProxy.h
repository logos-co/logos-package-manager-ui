#pragma once

#include <QSortFilterProxyModel>
#include <QHash>
#include <QString>

// Filter + sort proxy for the package catalog. Searches `name` + `description`,
// applies an install-state bucket filter, and sorts by a named role.
class PackagesFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit PackagesFilterProxy(QObject* parent = nullptr);

    // Plain text. Empty string = no search filter (all source rows pass).
    void setSearchText(const QString& text);
    QString searchText() const { return m_searchText; }

    // Install-state filter applied alongside the text search.
    //   0 = All (no filter)
    //   1 = Installed     (Installed / UpgradeAvailable / DowngradeAvailable
    //                      / DifferentHash / Installing — anything mid-life)
    //   2 = NotInstalled  (NotInstalled or Failed)
    void setInstallStateFilter(int state);
    int  installStateFilter() const { return m_installStateFilter; }

    // Package "type" filter — exact-match against the row's `type` role
    // (e.g. "ui", "core"). Empty string = no type filter.
    void setTypeFilter(const QString& type);
    QString typeFilter() const { return m_typeFilter; }

    // Sort by role *name* — looks up the role int via roleNames() and
    // delegates to QSortFilterProxyModel::sort.
    void setSortRoleByName(const QString& roleName);
    QString sortRoleName() const { return m_sortRoleName; }

    // Forward the underlying Qt::SortOrder (Qt::AscendingOrder = 0,
    // Qt::DescendingOrder = 1).
    void setSortOrderInt(int order);
    int  sortOrderInt() const { return static_cast<int>(m_sortOrder); }

    void setSourceModel(QAbstractItemModel* sourceModel) override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    // Rebuild m_roleByName + resolve every cached role-int from the new source.
    void recomputeRoleCaches();

    QString           m_searchText;
    int               m_installStateFilter = 0;
    QString           m_typeFilter;
    QString           m_sortRoleName;
    Qt::SortOrder     m_sortOrder = Qt::AscendingOrder;

    // Single source of truth for "role-name string → role-int" lookups.
    // Populated once per setSourceModel; consulted by filter / sort paths.
    QHash<QByteArray, int> m_roleByName;
    int m_typeFilterRole    = -1;
    int m_installStatusRole = -1;
    QList<int> m_searchRoles;          // resolved name + description
};
