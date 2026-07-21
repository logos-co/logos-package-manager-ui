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

void PackagesFilterProxy::setCategoryFilter(const QString& category)
{
    if (category == m_categoryFilter) return;
    m_categoryFilter = category;
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
    m_typeFilterRole     = -1;
    m_categoryFilterRole = -1;
    m_installStatusRole  = -1;
    m_repositoryNameRole        = -1;
    m_repositoryDisplayNameRole = -1;
    m_repositoryUrlRole         = -1;
    m_nameRole                  = -1;
    m_searchRoles.clear();
    if (!sourceModel()) return;

    const auto roles = sourceModel()->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        m_roleByName.insert(it.value(), it.key());

    m_typeFilterRole     = m_roleByName.value(QByteArrayLiteral("type"), -1);
    m_categoryFilterRole = m_roleByName.value(QByteArrayLiteral("category"), -1);
    m_installStatusRole  = m_roleByName.value(QByteArrayLiteral("installStatus"), -1);

    // Source-grouping roles. The lessThan override consults these to pin
    // the repo order ahead of the user-selected sort role; without them
    // resolved we fall back to plain sorting (acceptable for the unit
    // tests that don't surface multi-repo roles).
    m_repositoryNameRole        = m_roleByName.value(QByteArrayLiteral("repositoryName"), -1);
    m_repositoryDisplayNameRole = m_roleByName.value(QByteArrayLiteral("repositoryDisplayName"), -1);
    m_repositoryUrlRole         = m_roleByName.value(QByteArrayLiteral("repositoryUrl"), -1);
    m_nameRole                  = m_roleByName.value(QByteArrayLiteral("name"), -1);

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

    if (!m_categoryFilter.isEmpty() && m_categoryFilterRole >= 0) {
        const QString rowCategory =
            sourceModel()->data(idx, m_categoryFilterRole).toString();
        if (rowCategory.compare(m_categoryFilter, Qt::CaseInsensitive) != 0)
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

bool PackagesFilterProxy::lessThan(const QModelIndex& left,
                                   const QModelIndex& right) const
{
    // Source grouping is the primary sort key — regardless of which column
    // the user clicks to sort by. The default repository
    // ("logos-modules-official") always groups first; user repos follow,
    // sorted by displayName (case-insensitive). Within a group the
    // user-selected role (sortRole()) decides ordering. This mirrors the
    // backend's initial setPackagesFromVariantList ordering so the QML
    // section headers (`repositoryDisplayName`) keep producing one strip
    // per repo no matter how the user pivots the sort.
    //
    // Inversion note: QSortFilterProxyModel applies the active sortOrder
    // by flipping the comparator's result for descending. That'd also
    // flip the repo-group ordering, which the user does NOT want — repos
    // should keep their pinned order even when the user picks descending
    // on, say, the Package column. We compensate by inverting the
    // group-rank result when sortOrder is descending, so the *net* effect
    // after Qt's post-flip is the canonical group order. The within-group
    // role comparison is left untouched and gets the expected flip.
    if (!sourceModel())
        return QSortFilterProxyModel::lessThan(left, right);

    auto groupRank = [this](const QModelIndex& idx) -> std::pair<int, QString> {
        // Priority 0 = the canonical default repo (its `name` in
        // logos-repo.json is "logos-modules-official"), 1 = everyone else.
        QString name = (m_repositoryNameRole >= 0)
                           ? sourceModel()->data(idx, m_repositoryNameRole).toString()
                           : QString();
        const int pri = (name == QLatin1String("logos-modules-official")) ? 0 : 1;
        // Within the "everyone else" bucket, use displayName as the
        // grouping key with a name → URL fallback chain, mirroring the
        // backend's sourceKey().
        QString key;
        if (m_repositoryDisplayNameRole >= 0)
            key = sourceModel()->data(idx, m_repositoryDisplayNameRole).toString();
        if (key.isEmpty() && !name.isEmpty())
            key = name;
        if (key.isEmpty() && m_repositoryUrlRole >= 0)
            key = sourceModel()->data(idx, m_repositoryUrlRole).toString();
        return {pri, key};
    };

    const auto ra = groupRank(left);
    const auto rb = groupRank(right);

    if (ra.first != rb.first || ra.second.compare(rb.second, Qt::CaseInsensitive) != 0) {
        // Different repository — produce a result that, after Qt's
        // descending-flip, still puts default repo first.
        const bool asc = (ra.first < rb.first)
                         || (ra.first == rb.first
                             && ra.second.compare(rb.second, Qt::CaseInsensitive) < 0);
        return (m_sortOrder == Qt::DescendingOrder) ? !asc : asc;
    }

    // Same source group — fall through to the user-selected sort role.
    // When sortRole() is the default DisplayRole, QSortFilterProxyModel's
    // default lessThan handles the QVariant comparison sensibly; for
    // string-typed roles (`name`, `type`, etc.) it's a locale-aware
    // string compare. Either way we want it case-insensitive for stable
    // ordering — bypass the default and compare data() directly.
    const int role = sortRole();
    const QVariant la = sourceModel()->data(left,  role);
    const QVariant ra2 = sourceModel()->data(right, role);

    // Strings are by far the dominant case (package name, type,
    // description). Fall back to QVariant's operator< for numerics.
    if (la.userType() == QMetaType::QString && ra2.userType() == QMetaType::QString) {
        const int c = la.toString().compare(ra2.toString(), Qt::CaseInsensitive);
        if (c != 0) return c < 0;
    } else if (la != ra2) {
        return QSortFilterProxyModel::lessThan(left, right);
    }

    // Stable tiebreaker — within-group, within-role ties order by package
    // name so the same input produces the same row order across refreshes.
    if (m_nameRole >= 0) {
        const QString na = sourceModel()->data(left,  m_nameRole).toString();
        const QString nb = sourceModel()->data(right, m_nameRole).toString();
        return na.compare(nb, Qt::CaseInsensitive) < 0;
    }
    return false;
}
