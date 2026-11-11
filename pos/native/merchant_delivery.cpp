#include <jni.h>
#include "callback_registry.h"
#include <liblogosdelivery.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef MERCHANT_DELIVERY_DEBUG
#include <android/log.h>
#define DELIVERY_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "MerchantDelivery", __VA_ARGS__)
#else
#define DELIVERY_LOG(...) ((void)0)
#endif

namespace {
struct Events {
    std::mutex mutex;
    std::condition_variable available;
    std::deque<std::string> events;
#ifdef MERCHANT_DELIVERY_DEBUG
    uint64_t received = 0, dropped = 0, delivered = 0;
#endif
};
struct Node {
    LogosDeliveryCtx* ctx = nullptr;
    std::shared_ptr<Events> events = std::make_shared<Events>();
    uintptr_t eventTicket = 0;
    uint64_t listener = 0;
#ifdef MERCHANT_DELIVERY_DEBUG
    uint64_t sends = 0;
#endif
};
#ifdef MERCHANT_DELIVERY_DEBUG
bool logCount(uint64_t count) { return count && !(count & (count - 1)); }
#endif
struct Reply {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false, abandoned = false;
    int error = 0;
    LogosDeliveryCtx* ctx = nullptr;
};
using Shared = std::shared_ptr<Reply>;
template<class F> Shared invoke(F submit) {
    auto reply = std::make_shared<Reply>();
    auto& registry = CallbackRegistry<Reply>::instance();
    const auto ticket = registry.add(reply);
    // Generated C helpers can call back synchronously AND return an error.
    // Taking a ticket twice is harmless, unlike deleting callback user_data twice.
    if (submit(reinterpret_cast<void*>(ticket)) != RET_OK) {
        registry.take(ticket);
        throw std::runtime_error("Delivery operation could not be submitted");
    }
    std::unique_lock guard(reply->mutex);
    if (!reply->ready.wait_for(guard,std::chrono::seconds(45),[&]{return reply->done;})) {
        reply->abandoned = true;
        throw std::runtime_error("Delivery operation timed out; reconnect to retry");
    }
    if (reply->error != RET_OK) throw std::runtime_error("Delivery operation failed; check network configuration and service peers");
    return reply;
}
void onReply(int error, const char* const*, const char*, void* data) {
    auto reply = CallbackRegistry<Reply>::instance().take(reinterpret_cast<uintptr_t>(data));
    if (!reply) return;
    std::lock_guard guard(reply->mutex);
    reply->error = error; reply->done = true; reply->ready.notify_one();
}
void onCreate(int error, LogosDeliveryCtx* ctx, const char*, void* data) {
    auto reply = CallbackRegistry<Reply>::instance().take(reinterpret_cast<uintptr_t>(data));
    if (!reply) return;
    std::lock_guard guard(reply->mutex);
    reply->error = error; reply->ctx = ctx; reply->done = true;
    if ((reply->abandoned || error != RET_OK) && ctx) {
        reply->ctx = nullptr;
        std::thread([ctx]{logosdelivery_ctx_destroy(ctx);}).detach();
    }
    reply->ready.notify_one();
}
void event(int error, const char* data, size_t size, void* opaque) {
    if (error != RET_OK || !data || size > 98304) {
        DELIVERY_LOG("receive event rejected (invalid or oversized)");
        return;
    }
    try {
        auto events = CallbackRegistry<Events>::instance().get(reinterpret_cast<uintptr_t>(opaque));
        if (!events) return;
        std::lock_guard guard(events->mutex);
        if (events->events.size() >= 32) {
            events->events.pop_front();
#ifdef MERCHANT_DELIVERY_DEBUG
            if (logCount(++events->dropped)) DELIVERY_LOG("receive queue overflow: dropped=%llu", static_cast<unsigned long long>(events->dropped));
#endif
        }
        events->events.emplace_back(data,size); events->available.notify_one();
#ifdef MERCHANT_DELIVERY_DEBUG
        if (logCount(++events->received)) DELIVERY_LOG("receive events queued=%llu depth=%zu", static_cast<unsigned long long>(events->received), events->events.size());
#endif
    } catch (...) { /* Never unwind C++ exceptions through the Nim event loop. */ }
}
struct Utf {
    JNIEnv* env; jstring value; const char* data;
    Utf(JNIEnv* e,jstring v):env(e),value(v),data(e->GetStringUTFChars(v,nullptr)) { if (!data) throw std::runtime_error("Invalid text"); }
    ~Utf(){env->ReleaseStringUTFChars(value,data);}
};
Node* get(jlong handle) { if (!handle) throw std::runtime_error("Delivery node is not started"); return reinterpret_cast<Node*>(handle); }
void fail(JNIEnv* env,const std::exception& error) { env->ThrowNew(env->FindClass("java/io/IOException"),error.what()); }
void destroy(Node* node) {
    if (!node) return;
    DELIVERY_LOG("node stopping");
    // Invalidate the callback target before asking upstream to stop. Even a
    // timed-out/quarantined context cannot access a freed mutex afterwards.
    CallbackRegistry<Events>::instance().take(node->eventTicket);
    if (node->ctx) {
        if (node->listener) logosdelivery_remove_event_listener(node->ctx->ptr,node->listener);
        try { invoke([&](void* data){return logosdelivery_ctx_stop_node(node->ctx,onReply,data);}); } catch (...) {}
        // An already-dispatched callback holds its own shared event state.
        logosdelivery_ctx_destroy(node->ctx);
    }
    delete node;
    DELIVERY_LOG("node stopped");
}
}
extern "C" JNIEXPORT jlong JNICALL Java_org_moneromerchant_pos_NativeLib_open(JNIEnv* env,jobject,jstring config) {
    auto node = std::make_unique<Node>();
    DELIVERY_LOG("node starting");
    try {
        Utf text(env,config);
        node->ctx = invoke([&](void* data){return logosdelivery_ctx_create(text.data,onCreate,data);})->ctx;
        if (!node->ctx) throw std::runtime_error("Could not create Delivery node");
        node->eventTicket = CallbackRegistry<Events>::instance().add(node->events);
        node->listener = logosdelivery_add_event_listener(node->ctx->ptr,"onMessageReceived",event,reinterpret_cast<void*>(node->eventTicket));
        if (!node->listener) throw std::runtime_error("Could not register Delivery listener");
        invoke([&](void* data){return logosdelivery_ctx_start_node(node->ctx,onReply,data);});
        DELIVERY_LOG("node started");
        return reinterpret_cast<jlong>(node.release());
    } catch (const std::exception& error) { DELIVERY_LOG("node startup failed"); destroy(node.release()); fail(env,error); return 0; }
}
extern "C" JNIEXPORT void JNICALL Java_org_moneromerchant_pos_NativeLib_subscribe(JNIEnv* env,jobject,jlong handle,jstring topic) {
    DELIVERY_LOG("reply subscription starting");
    try { Utf text(env,topic); auto node=get(handle); invoke([&](void* data){return logosdelivery_ctx_subscribe(node->ctx,text.data,onReply,data);}); DELIVERY_LOG("reply subscription accepted"); }
    catch (const std::exception& error){DELIVERY_LOG("reply subscription failed"); fail(env,error);}
}
extern "C" JNIEXPORT void JNICALL Java_org_moneromerchant_pos_NativeLib_send(JNIEnv* env,jobject,jlong handle,jstring message) {
    try { Utf text(env,message); auto node=get(handle); invoke([&](void* data){return logosdelivery_ctx_send(node->ctx,text.data,onReply,data);});
#ifdef MERCHANT_DELIVERY_DEBUG
        if (logCount(++node->sends)) DELIVERY_LOG("sends accepted=%llu", static_cast<unsigned long long>(node->sends));
#endif
    }
    catch (const std::exception& error){DELIVERY_LOG("send failed"); fail(env,error);}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_moneromerchant_pos_NativeLib_receive(JNIEnv* env,jobject,jlong handle,jint timeoutMs) {
    try {
        auto events=get(handle)->events; std::unique_lock guard(events->mutex);
        events->available.wait_for(guard,std::chrono::milliseconds(timeoutMs),[&]{return !events->events.empty();});
        if (events->events.empty()) return nullptr;
        auto text=std::move(events->events.front()); events->events.pop_front();
#ifdef MERCHANT_DELIVERY_DEBUG
        if (logCount(++events->delivered)) DELIVERY_LOG("receive events delivered=%llu", static_cast<unsigned long long>(events->delivered));
#endif
        return env->NewStringUTF(text.c_str());
    } catch (const std::exception& error){fail(env,error);return nullptr;}
}
extern "C" JNIEXPORT void JNICALL Java_org_moneromerchant_pos_NativeLib_close(JNIEnv* env,jobject,jlong handle) {
    try {destroy(reinterpret_cast<Node*>(handle));} catch (const std::exception& error){fail(env,error);}
}
