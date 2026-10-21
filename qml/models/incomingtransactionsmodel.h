// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_INCOMINGTRANSACTIONSMODEL_H
#define BITCOIN_QML_MODELS_INCOMINGTRANSACTIONSMODEL_H

#include <interfaces/chain.h>
#include <interfaces/handler.h>

#include <QList>
#include <QObject>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

namespace interfaces {
class Node;
}

class QThread;
class QTimer;

namespace incoming_transactions {

//! Seconds of transactions each sample averages over, like mempool.space.
static constexpr int64_t WINDOW_SECONDS{150};
//! Two hours of samples, one a minute.
static constexpr size_t MAX_SAMPLES{120};

struct Sample {
    //! In seconds since the epoch.
    int64_t time{0};
    double vbytes_per_second{0.0};
};

//! Virtual size of the transactions entering the mempool, sampled like
//! mempool.space's incoming transactions chart: each sample is the vbytes
//! per second of the transactions added in the 150 seconds before it.
class Rate
{
public:
    //! Count transactions from start_time, in seconds since the epoch.
    explicit Rate(int64_t start_time) : m_start_time{start_time} {}

    void add(int64_t time, int64_t vsize);
    //! Record a sample at time. Until 150 seconds have been counted, it
    //! averages over the time counted so far.
    void sample(int64_t time);
    const std::deque<Sample>& samples() const { return m_samples; }

private:
    int64_t m_start_time;
    //! Virtual size added in each second of the window.
    std::deque<std::pair<int64_t, int64_t>> m_recent;
    std::deque<Sample> m_samples;
};

} // namespace incoming_transactions

//! Rate at which transactions enter the node's mempool, recorded from when
//! the node started. The mempool saved at the last shutdown is loaded again
//! at startup, and isn't counted.
class IncomingTransactionsModel : public QObject
{
    Q_OBJECT
    //! Sample times in milliseconds since the epoch, oldest first, and their
    //! vbytes per second.
    Q_PROPERTY(QList<qreal> times READ times NOTIFY samplesChanged)
    Q_PROPERTY(QList<qreal> rates READ rates NOTIFY samplesChanged)
    //! When counting started, in milliseconds since the epoch, or 0 while the
    //! saved mempool loads.
    Q_PROPERTY(qint64 recordingSince READ recordingSince NOTIFY recordingSinceChanged)

public:
    using HandleNotificationsFn = std::function<std::unique_ptr<interfaces::Handler>(std::shared_ptr<interfaces::Chain::Notifications>)>;
    //! Waits until the notifications queued so far are delivered.
    using WaitForNotificationsFn = std::function<void()>;

    IncomingTransactionsModel(interfaces::Node& node, HandleNotificationsFn handle_notifications,
                              WaitForNotificationsFn wait_for_notifications, int sample_interval_ms = 60'000,
                              QObject* parent = nullptr);
    ~IncomingTransactionsModel() override;

    QList<qreal> times() const { return m_times; }
    QList<qreal> rates() const { return m_rates; }
    qint64 recordingSince() const { return m_recording_since; }

    void beginShutdown();

public Q_SLOTS:
    //! Start counting once the saved mempool is loaded.
    void start();

Q_SIGNALS:
    void drained();
    void samplesChanged();
    void recordingSinceChanged();

private:
    struct State;

    void startRecording(int64_t start_time);
    void sample();
    void stopLoadThread();

    interfaces::Node& m_node;
    HandleNotificationsFn m_handle_notifications;
    WaitForNotificationsFn m_wait_for_notifications;
    std::shared_ptr<State> m_state;
    std::unique_ptr<interfaces::Handler> m_handler;
    QThread* m_load_thread{nullptr};
    QTimer* m_sample_timer;
    QList<qreal> m_times;
    QList<qreal> m_rates;
    qint64 m_recording_since{0};
    bool m_started{false};
    bool m_stopping{false};
};

#endif // BITCOIN_QML_MODELS_INCOMINGTRANSACTIONSMODEL_H
