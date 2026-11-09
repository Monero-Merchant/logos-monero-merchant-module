#pragma once
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace merchant
{
struct InvoiceError : std::runtime_error
{
    std::string code;
    InvoiceError(std::string code, const std::string& message)
        : std::runtime_error(message), code(std::move(code))
    {
    }
};

class InvoiceStore
{
public:
    explicit InvoiceStore(const std::string& directory);
    ~InvoiceStore();
    InvoiceStore(const InvoiceStore&) = delete;
    InvoiceStore& operator=(const InvoiceStore&) = delete;
    int confirmationPolicy() const;
    void setConfirmationPolicy(int64_t confirmations);
    nlohmann::json find(const std::string& id) const;
    nlohmann::json reserve(const std::string& id, const std::string& title, const std::string& atomic,
                           const nlohmann::json& wallet);
    void markAttempted(const std::string& id);
    void bind(const std::string& id, uint32_t index, const std::string& address);
    void recoveryRequired(const std::string& id, const std::string& message);
    void reconcile(const nlohmann::json& snapshot);
    void acknowledgeReview(const std::string& id);
    nlohmann::json snapshot(const nlohmann::json& monitor) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace merchant
