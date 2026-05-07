#include "PackagesFilterProxy.h"

#include <QAbstractItemModel>

PackagesFilterProxy::PackagesFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

// ───────────────────────────── filter ─────────────────────────────

void PackagesFilterProxy::setSearchText(const QString& text)
{
    if (text == m_searchText) return;
    m_searchText = text;
    invalidateFilter();
}

void PackagesFilterProxy::setInstallStateFilter(int state)
{
    if (state == m_installStateFilter) return;
    m_installStateFilter = state;
    invalidateFilter();
}

void PackagesFilterProxy::setTypeFilter(const QString& type)
{
    if (type == m_typeFilter) return;
    m_typeFilter = type;
    invalidateFilter();
}

void PackagesFilterProxy::setSourceModel(QAbstractItemModel* sourceModel)
{
    QSortFilterProxyModel::setSourceModel(sourceModel);
    recomputeRoleCaches();
}

void PackagesFilterProxy::recomputeRoleCaches()
{
    m_roleByName.clear();
    m_typeFilterRole    = -1;
    m_installStatusRole = -1;
    m_searchRoles.clear();
    if (!sourceModel()) return;

    const auto roles = sourceModel()->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        m_roleByName.insert(it.value(), it.key());

    m_typeFilterRole    = m_roleByName.value(QByteArrayLiteral("type"), -1);
    m_installStatusRole = m_roleByName.value(QByteArrayLiteral("installStatus"), -1);

    for (const QByteArray& name : { QByteArrayLiteral("name"),
                                    QByteArrayLiteral("description") }) {
        const int r = m_roleByName.value(name, -1);
        if (r >= 0) m_searchRoles.append(r);
    }

    // Re-resolve the active sort role against the new source. Falls back to
    // "no sort" if the role isn't present rather than mis-sorting.
    if (!m_sortRoleName.isEmpty())
        setSortRoleByName(m_sortRoleName);
}

bool PackagesFilterProxy::filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const
{
    if (!sourceModel()) return true;
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);

    if (!m_typeFilter.isEmpty() && m_typeFilterRole >= 0) {
        const QString rowType = sourceModel()->data(idx, m_typeFilterRole).toString();
        if (rowType.compare(m_typeFilter, Qt::CaseInsensitive) != 0)
            return false;
    }

    // Install-state filter. Looked up by role NAME (not by InstallStatusRole
    // enum) to avoid a hard #include dependency on the model.
    if (m_installStateFilter != 0 && m_installStatusRole >= 0) {
        const int status = sourceModel()->data(idx, m_installStatusRole).toInt();
        const bool isInstalledBucket = (status != 0 && status != 3);
        if (m_installStateFilter == 1 && !isInstalledBucket) return false;
        if (m_installStateFilter == 2 &&  isInstalledBucket) return false;
    }

    // Text search. Empty query OR no resolved search roles → no filter.
    if (m_searchText.isEmpty() || m_searchRoles.isEmpty()) return true;
    for (int role : m_searchRoles) {
        const QString cell = sourceModel()->data(idx, role).toString();
        if (cell.contains(m_searchText, Qt::CaseInsensitive)) return true;
    }
    return false;
}

// ───────────────────────────── sort ───────────────────────────────

void PackagesFilterProxy::setSortRoleByName(const QString& roleName)
{
    m_sortRoleName = roleName;
    if (!sourceModel()) return;

    if (roleName.isEmpty()) {
        setSortRole(Qt::DisplayRole);
        sort(-1);                                    // disable sorting
        return;
    }

    const int found = m_roleByName.value(roleName.toUtf8(), -1);
    if (found < 0) {
        // Unknown role — reset to no sort rather than mis-sorting.
        setSortRole(Qt::DisplayRole);
        sort(-1);
    } else {
        setSortRole(found);
        sort(0, m_sortOrder);
    }
}

void PackagesFilterProxy::setSortOrderInt(int order)
{
    const Qt::SortOrder o = (order == Qt::DescendingOrder)
                                ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (o == m_sortOrder) return;
    m_sortOrder = o;
    if (!m_sortRoleName.isEmpty())
        sort(0, m_sortOrder);
}
