#include "incoming_monitor.h"
#include "incoming_payments.h"

#include <QDateTime>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>

namespace
{
using Json = nlohmann::json;
Json walletView(const Json& status)
{
    return {{"id", merchant::walletIdentity(status)},
            {"name", status.at("wallet")},
            {"network", status.at("network")},
            {"address", status.at("address")},
            {"connected", status.contains("connected") && status["connected"].is_boolean()
                              ? status["connected"]
                              : Json(nullptr)},
            {"synchronized", status.contains("synchronized") && status["synchronized"].is_boolean()
                                 ? status["synchronized"]
                                 : Json(nullptr)}};
}
class Monitor final : public QObject, public IncomingMonitor
{
public:
    Monitor(Read read, Notify notify) : read_(std::move(read)), notify_(std::move(notify))
    {
        cache_ = {{"ok", true},        {"state", "starting"},  {"stale", false}, {"payments", Json::array()},
                  {"wallet", nullptr}, {"updated_at", nullptr}};
        timer_.setInterval(10000);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { request(); });
        timer_.start();
        QTimer::singleShot(0, this, [this] { request(); });
    }
    ~Monitor() override
    {
        stop();
    }
    std::string snapshot() const override
    {
        std::lock_guard<std::mutex> guard(mu_);
        return cache_.dump();
    }
    void refresh() override
    {
        QPointer<Monitor> self(this);
        QMetaObject::invokeMethod(
            this,
            [self]
            {
                if (self)
                    self->request();
            },
            Qt::QueuedConnection);
    }
    void stop() override
    {
        if (stopped_.exchange(true))
            return;
        if (QThread::currentThread() == thread())
            timer_.stop();
        else
            QMetaObject::invokeMethod(this, [this] { timer_.stop(); }, Qt::BlockingQueuedConnection);
        notify_ = {};
    }

private:
    Read read_;
    Notify notify_;
    QTimer timer_;
    mutable std::mutex mu_;
    Json cache_;
    std::atomic<bool> stopped_{false};
    bool active_ = false, dirty_ = false;

    void publish(Json next)
    {
        next["updated_at"] = QDateTime::currentMSecsSinceEpoch();
        std::string payload;
        {
            std::lock_guard<std::mutex> guard(mu_);
            cache_ = std::move(next);
            payload = cache_.dump();
        }
        if (!stopped_ && notify_)
            notify_(payload);
    }
    void finish()
    {
        active_ = false;
        if (dirty_ && !stopped_)
        {
            dirty_ = false;
            // Coalesce bursts of balance/sync/state events into one next read.
            QTimer::singleShot(100, this, [this] { request(); });
        }
    }
    void failure(Json reply, bool keepRows = false)
    {
        Json next = Json::parse(snapshot());
        next["ok"] = false;
        next["state"] = "error";
        next["stale"] = keepRows && !next["payments"].empty();
        next["error"] = reply.contains("error") && reply["error"].is_string()
                            ? reply["error"]
                            : Json("The wallet could not be read.");
        next["code"] =
            reply.contains("code") && reply["code"].is_string() ? reply["code"] : Json("wallet_error");
        if (!keepRows)
        {
            next["payments"] = Json::array();
            next["wallet"] = nullptr;
        }
        publish(std::move(next));
        finish();
    }
    static Json parse(const std::string& raw)
    {
        auto reply = Json::parse(raw, nullptr, false);
        if (!reply.is_object() || !reply.contains("ok") || !reply["ok"].is_boolean())
            return {{"ok", false}, {"code", "invalid_wallet_response"}, {"error", "Invalid wallet reply."}};
        return reply;
    }
    void read(const std::string& method, std::function<void(Json)> callback)
    {
        QPointer<Monitor> self(this);
        auto complete = [self, callback = std::move(callback)](std::string raw)
        {
            if (!self || self->stopped_)
                return;
            QMetaObject::invokeMethod(
                self,
                [self, callback, raw = std::move(raw)]
                {
                    if (self && !self->stopped_)
                        callback(parse(raw));
                },
                Qt::QueuedConnection);
        };
        try
        {
            read_(method, complete);
        }
        catch (const std::exception& e)
        {
            complete(Json{{"ok", false}, {"code", "ipc_error"}, {"error", e.what()}}.dump());
        }
    }
    bool usable(const Json& status)
    {
        if (!status.value("ok", false))
        {
            failure(status);
            return false;
        }
        const bool open = status.contains("wallet") && status["wallet"].is_string() &&
                          !status["wallet"].get<std::string>().empty();
        if (!open || !status.contains("network") || status["network"] != "stagenet")
        {
            const auto state = open ? "wrong_network" : "no_wallet";
            publish({{"ok", true},
                     {"state", state},
                     {"stale", false},
                     {"payments", Json::array()},
                     {"wallet", nullptr}});
            finish();
            return false;
        }
        try
        {
            (void)merchant::walletIdentity(status);
        }
        catch (const std::exception& e)
        {
            failure({{"error", e.what()}, {"code", "invalid_wallet_response"}});
            return false;
        }
        return true;
    }
    void request()
    {
        if (stopped_)
            return;
        if (active_)
        {
            dirty_ = true;
            return;
        }
        active_ = true;
        read("wallet_status",
             [this](Json before)
             {
                 if (!usable(before))
                     return;
                 const auto identity = merchant::walletIdentity(before);
                 auto previous = Json::parse(snapshot());
                 if (previous["wallet"].is_null() || previous["wallet"].value("id", "") != identity)
                 {
                     publish({{"ok", true},
                              {"state", "loading"},
                              {"stale", false},
                              {"payments", Json::array()},
                              {"wallet", walletView(before)}});
                 }
                 read("history",
                      [this, before](Json history)
                      {
                          if (!history.value("ok", false))
                          {
                              failure(history, true);
                              return;
                          }
                          read("wallet_status",
                               [this, before, history](Json after)
                               {
                                   if (!usable(after))
                                       return;
                                   if (merchant::walletIdentity(before) != merchant::walletIdentity(after))
                                   {
                                       // History was read across a wallet switch: never publish it.
                                       publish({{"ok", true},
                                                {"state", "loading"},
                                                {"stale", false},
                                                {"payments", Json::array()},
                                                {"wallet", nullptr}});
                                       dirty_ = true;
                                       finish();
                                       return;
                                   }
                                   try
                                   {
                                       auto payments = merchant::incomingPayments(after, history);
                                       publish({{"ok", true},
                                                {"state", "ready"},
                                                {"stale", false},
                                                {"payments", std::move(payments)},
                                                {"wallet", walletView(after)}});
                                   }
                                   catch (const std::exception& e)
                                   {
                                       failure({{"code", "invalid_history"}, {"error", e.what()}}, true);
                                       return;
                                   }
                                   finish();
                               });
                      });
             });
    }
};
} // namespace
std::shared_ptr<IncomingMonitor> makeIncomingMonitor(IncomingMonitor::Read read,
                                                     IncomingMonitor::Notify notify)
{
    return std::make_shared<Monitor>(std::move(read), std::move(notify));
}
