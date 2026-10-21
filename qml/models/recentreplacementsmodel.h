// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RECENTREPLACEMENTSMODEL_H
#define BITCOIN_QML_MODELS_RECENTREPLACEMENTSMODEL_H

#include <coins.h>
#include <consensus/amount.h>
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <primitives/transaction.h>

#include <QAbstractListModel>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QThread;

namespace replacements {

//! Replacements shown, like mempool.space's dashboard.
static constexpr size_t MAX_REPLACEMENTS{6};

struct Replacement {
    //! The replacing transaction.
    Txid txid;
    //! Fees and virtual sizes of the transactions it replaced, and its own.
    //! A fee is unknown when a coin it spends can no longer be found.
    std::optional<CAmount> old_fee;
    int64_t old_vsize{0};
    std::optional<CAmount> new_fee;
    int64_t new_vsize{0};
    //! A replaced transaction did not signal replaceability (BIP125).
    bool full_rbf{false};
    bool mined{false};
};

//! The latest replacements, newest first. Like on mempool.space, a
//! replacement that is replaced again gives way to the newer one.
class ReplacementList
{
public:
    void add(const std::vector<Txid>& replaced, Replacement replacement);
    //! Returns whether txid is a replacement in the list.
    bool markMined(const Txid& txid);
    const std::deque<Replacement>& replacements() const { return m_replacements; }

private:
    std::deque<Replacement> m_replacements;
};

} // namespace replacements

//! The latest transactions that replaced others in the node's mempool, since
//! the node started.
class RecentReplacementsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        TxidRole = Qt::UserRole + 1,
        //! In sat/vB, or -1 when unknown.
        OldFeeRateRole,
        NewFeeRateRole,
        FullRbfRole,
        MinedRole,
    };

    using HandleNotificationsFn = std::function<std::unique_ptr<interfaces::Handler>(std::shared_ptr<interfaces::Chain::Notifications>)>;
    //! Chain::findCoins(): fills in the coins, from the UTXO set or the
    //! mempool, and clears the ones it can't find.
    using FindCoinsFn = std::function<void(std::map<COutPoint, Coin>&)>;

    RecentReplacementsModel(HandleNotificationsFn handle_notifications, FindCoinsFn find_coins, QObject* parent = nullptr);
    ~RecentReplacementsModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_replacements.size()); }

    void setReplacements(std::vector<replacements::Replacement> replacements);
    void beginShutdown();

public Q_SLOTS:
    void start();

Q_SIGNALS:
    void drained();
    void countChanged();

private:
    struct State;

    void stopWorker();

    HandleNotificationsFn m_handle_notifications;
    FindCoinsFn m_find_coins;
    std::shared_ptr<State> m_state;
    std::unique_ptr<interfaces::Handler> m_handler;
    QThread* m_worker_thread{nullptr};
    std::vector<replacements::Replacement> m_replacements;
    bool m_started{false};
    bool m_stopping{false};
};

#endif // BITCOIN_QML_MODELS_RECENTREPLACEMENTSMODEL_H
