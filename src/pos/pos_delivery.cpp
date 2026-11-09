#include "pos_delivery.h"
#include "logos_sdk.h"
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
namespace merchant::pos
{
namespace
{
struct Inbox
{
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::string> queue;
    bool stopped = false;
};
class Runtime final : public QObject, public Delivery
{
public:
    Runtime(LogosModules& modules, const std::string& directory, Service::Backend backend)
        : modules_(modules), service_(directory, std::move(backend))
    {
        QPointer<Runtime> self(this);
        handles_.push_back(modules_.delivery_module.onNodeStarted(
            [self](bool ok, const std::string&, int64_t)
            {
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, ok]
                        {
                            if (self && !self->stopped_)
                            {
                                self->startPending_ = false;
                                self->started_ = ok;
                                self->startupFailed_ = !ok;
                                if (ok)
                                    self->subscribe();
                                else
                                    self->failed();
                            }
                        },
                        Qt::QueuedConnection);
            }));
        handles_.push_back(modules_.delivery_module.onNodeStopped(
            [self](bool, const std::string&, int64_t)
            {
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self]
                        {
                            if (self && !self->stopped_)
                            {
                                self->started_ = false;
                                self->startPending_ = false;
                                self->subscribed_ = false;
                                self->failed();
                            }
                        },
                        Qt::QueuedConnection);
            }));
        handles_.push_back(modules_.delivery_module.onConnectionStateChanged(
            [self](const std::string&, int64_t)
            {
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self]
                        {
                            if (!self || self->stopped_)
                                return;
                            self->ready_ = false;
                            if (!self->busy_)
                                self->connectNode();
                        },
                        Qt::QueuedConnection);
            }));
        auto inbox = inbox_;
        const auto requestTopic = service_.topic();
        handles_.push_back(modules_.delivery_module.onMessageReceived(
            [inbox, requestTopic](const std::string&, const std::string& topic,
                                  const std::vector<uint8_t>& payload, const std::string&, int64_t)
            {
                if (topic != requestTopic || payload.size() > 49152)
                    return;
                // Inbox lifetime is independent of Runtime, including in-flight callbacks.
                {
                    std::lock_guard<std::mutex> lock(inbox->mutex);
                    if (inbox->stopped || inbox->queue.size() >= 32)
                        return;
                    inbox->queue.emplace_back(payload.begin(), payload.end());
                }
                inbox->condition.notify_one();
            }));
        modules_.delivery_module.onSubscriptionStatus(
            [self](logos::SubStatus status, uint64_t)
            {
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, status]
                        {
                            if (!self || self->stopped_)
                                return;
                            if (status == logos::SubStatus::Lost)
                            {
                                self->created_ = false;
                                self->started_ = false;
                                self->startPending_ = false;
                                self->subscribed_ = false;
                                self->shared_ = false;
                                self->failed();
                            }
                            else
                                self->connectNode();
                        },
                        Qt::QueuedConnection);
            });
        worker_ = std::thread(
            [this, self]
            {
                for (;;)
                {
                    std::string raw;
                    {
                        std::unique_lock<std::mutex> lock(inbox_->mutex);
                        inbox_->condition.wait(lock,
                                               [this] { return inbox_->stopped || !inbox_->queue.empty(); });
                        if (stopped_)
                            return;
                        raw = std::move(inbox_->queue.front());
                        inbox_->queue.pop_front();
                    }
                    try
                    {
                        auto reply = service_.handle(raw);
                        if (self)
                            QMetaObject::invokeMethod(
                                self,
                                [self, reply = std::move(reply)]
                                {
                                    if (!self || self->stopped_)
                                        return;
                                    std::vector<uint8_t> bytes(reply.second.begin(), reply.second.end());
                                    self->modules_.delivery_module.sendAsyncResult(
                                        reply.first, bytes,
                                        [self](auto result)
                                        {
                                            if ((!result.error.ok() || !result.value.success) && self)
                                                QMetaObject::invokeMethod(
                                                    self,
                                                    [self]
                                                    {
                                                        if (self)
                                                            self->failed();
                                                    },
                                                    Qt::QueuedConnection);
                                        },
                                        5000);
                                },
                                Qt::QueuedConnection);
                    }
                    catch (...)
                    { /* Invalid messages and uncertain requests receive no plaintext oracle. */
                    }
                }
            });
        QObject::connect(&retry_, &QTimer::timeout, this, [this] { connectNode(); });
        retry_.start(5000);
        QTimer::singleShot(0, this, [this] { connectNode(); });
    }
    ~Runtime() override
    {
        stop();
    }
    void stop() override
    {
        if (stopped_.exchange(true))
            return;
        retry_.stop();
        ready_ = false;
        modules_.delivery_module.onSubscriptionStatus({});
        for (const auto& handle : handles_)
            handle.cancel();
        {
            std::lock_guard<std::mutex> lock(inbox_->mutex);
            inbox_->stopped = true;
            inbox_->queue.clear();
        }
        inbox_->condition.notify_all();
        if (worker_.joinable())
            worker_.join();
        // Release only our subscription. Other applications may share the node.
        modules_.delivery_module.unsubscribeAsyncResult(service_.topic(), [](auto) {}, 5000);
    }
    Json snapshot() override
    {
        auto result = service_.snapshot();
        result["ready"] = ready_.load();
        result["state"] = ready_ ? "ready" : startupFailed_ ? "failed" : "connecting";
        if (!ready_ && startupFailed_)
            result["error"] = "Delivery could not start. Check that the compatible discovery module from "
                              "this repository is installed, then restart Basecamp.";
        return result;
    }
    Json invitation() override
    {
        if (!ready_)
            return {
                {"ok", false},
                {"error", "Logos Delivery is connecting. Wait and refresh before generating an invitation."}};
        return service_.invitation();
    }
    Json authorize(const std::string& peer, bool approve) override
    {
        return service_.authorize(peer, approve);
    }

private:
    void failed()
    {
        ready_ = false;
        busy_ = false;
        ++generation_;
    }
    void connectNode()
    {
        if (stopped_ || ready_ || busy_)
            return;
        busy_ = true;
        const auto generation = ++generation_;
        QPointer<Runtime> self(this);
        if (created_)
        {
            if (started_ || startPending_ || shared_)
                checkConnection();
            else
                startNode();
            return;
        }
        const auto config =
            Json{{"mode", "Edge"},
                 {"preset", "logos.dev"},
                 {"messagingOverrides",
                  {{"anonymityLevel", "None"}, {"plugin-kad-discovery", true}, {"nat", "none"}}}}
                .dump();
        modules_.delivery_module.createNodeAsyncResult(
            config,
            [self, generation](auto result)
            {
                const bool ok = result.error.ok() && result.value.success;
                const bool existing =
                    result.error.ok() && result.value.error == "Context already initialized";
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, generation, ok, existing]
                        {
                            if (!self || self->stopped_ || generation != self->generation_)
                                return;
                            if (!ok && !existing)
                            {
                                self->failed();
                                return;
                            }
                            self->created_ = true;
                            self->shared_ = existing;
                            // A shared node may already be running or starting. Never restart
                            // it based only on a successful topic subscription.
                            if (existing)
                                self->checkConnection();
                            else
                                self->startNode();
                        },
                        Qt::QueuedConnection);
            },
            15000);
    }
    void startNode()
    {
        if (stopped_ || startPending_)
            return;
        startPending_ = true;
        QPointer<Runtime> self(this);
        const auto generation = generation_;
        modules_.delivery_module.startAsyncResult(
            [self, generation](auto result)
            {
                const bool refused = result.error.ok() && !result.value.success;
                if ((!result.error.ok() || refused) && self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, generation, refused]
                        {
                            if (!self || self->stopped_ || generation != self->generation_)
                                return;
                            // An IPC timeout is uncertain: keep waiting for the start
                            // event or connection status rather than issuing start twice.
                            if (refused)
                                self->startPending_ = false;
                            self->startupFailed_ = true;
                            self->failed();
                        },
                        Qt::QueuedConnection);
            },
            10000);
        QTimer::singleShot(30000, this,
                           [this, generation]
                           {
                               if (!stopped_ && busy_ && generation == generation_)
                                   checkConnection();
                           });
    }
    void subscribe()
    {
        if (stopped_)
            return;
        QPointer<Runtime> self(this);
        const auto generation = generation_;
        modules_.delivery_module.subscribeAsyncResult(
            service_.topic(),
            [self, generation](auto result)
            {
                const bool ok = result.error.ok() && result.value.success;
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, generation, ok]
                        {
                            if (!self || self->stopped_ || generation != self->generation_)
                                return;
                            self->subscribed_ = ok;
                            if (ok)
                                self->checkConnection();
                            else
                                self->failed();
                        },
                        Qt::QueuedConnection);
            },
            10000);
    }
    void checkConnection()
    {
        if (stopped_)
            return;
        QPointer<Runtime> self(this);
        const auto generation = generation_;
        modules_.delivery_module.getConnectionStatusAsyncResult(
            [self, generation](auto result)
            {
                const bool connected =
                    result.error.ok() && result.value.success &&
                    (result.value.value == "Connected" || result.value.value == "PartiallyConnected");
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, generation, connected]
                        {
                            if (!self || self->stopped_ || generation != self->generation_)
                                return;
                            if (!connected)
                            {
                                self->ready_ = false;
                                self->busy_ = false;
                                return;
                            }
                            self->started_ = true;
                            self->startPending_ = false;
                            self->startupFailed_ = false;
                            if (!self->subscribed_)
                            {
                                self->subscribe();
                                return;
                            }
                            self->ready_ = true;
                            self->busy_ = false;
                        },
                        Qt::QueuedConnection);
            },
            5000);
    }
    LogosModules& modules_;
    Service service_;
    std::vector<logos::SubHandle> handles_;
    std::atomic<bool> stopped_{false}, ready_{false}, startupFailed_{false};
    bool created_ = false, busy_ = false, started_ = false, startPending_ = false, subscribed_ = false,
         shared_ = false;
    uint64_t generation_ = 0;
    QTimer retry_;
    std::shared_ptr<Inbox> inbox_ = std::make_shared<Inbox>();
    std::thread worker_;
};
} // namespace
std::shared_ptr<Delivery> startDelivery(LogosModules& modules, const std::string& directory,
                                        Service::Backend backend)
{
    return std::make_shared<Runtime>(modules, directory, std::move(backend));
}
} // namespace merchant::pos
