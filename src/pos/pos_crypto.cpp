#include "pos_crypto.h"
#include <chrono>
#include <memory>
#include <openssl/evp.h>
#include <openssl/hpke.h>
#include <openssl/rand.h>
#include <regex>
#include <stdexcept>
namespace merchant::pos
{
namespace
{
void check(bool success)
{
    if (!success)
        throw std::runtime_error("Invalid encrypted POS message");
}
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Context = std::unique_ptr<OSSL_HPKE_CTX, decltype(&OSSL_HPKE_CTX_free)>;
const OSSL_HPKE_SUITE suite{OSSL_HPKE_KEM_ID_X25519, OSSL_HPKE_KDF_ID_HKDF_SHA256,
                            OSSL_HPKE_AEAD_ID_AES_GCM_128};
const std::string info = "monero-merchant/delivery/2/hpke-auth";
const unsigned char* ptr(const std::string& s)
{
    return reinterpret_cast<const unsigned char*>(s.data());
}
unsigned char* ptr(std::string& s)
{
    return reinterpret_cast<unsigned char*>(s.data());
}
Key privateKey(const std::string& raw)
{
    check(raw.size() == 32);
    Key key(EVP_PKEY_new_raw_private_key_ex(nullptr, "X25519", nullptr, ptr(raw), raw.size()), EVP_PKEY_free);
    check(bool(key));
    return key;
}
std::string aad(const std::string& direction, const std::string& topic, const Json& envelope)
{
    check(uuid(envelope.at("id").get<std::string>()) && topic.size() <= 200 &&
          topic.find('\n') == std::string::npos);
    return "2\n" + direction + "\n" + topic + "\n" + envelope.at("sender").get<std::string>() + "\n" +
           envelope.at("recipient").get<std::string>() + "\n" + envelope.at("id").get<std::string>() + "\n" +
           std::to_string(envelope.at("issued_at").get<int64_t>());
}
} // namespace
int64_t now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
bool uuid(const std::string& s)
{
    static const std::regex pattern("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
    return std::regex_match(s, pattern);
}
std::string randomBytes(size_t n)
{
    check(n <= 65536);
    std::string result(n, '\0');
    check(RAND_bytes(ptr(result), static_cast<int>(n)) == 1);
    return result;
}
std::string encode(const std::string& s)
{
    std::string out(4 * ((s.size() + 2) / 3) + 1, '\0');
    EVP_EncodeBlock(ptr(out), ptr(s), static_cast<int>(s.size()));
    out.resize(out.size() - 1);
    for (auto& c : out)
    {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    while (!out.empty() && out.back() == '=')
        out.pop_back();
    return out;
}
std::string decode(const std::string& s)
{
    check(s.size() <= 65536 && s.size() % 4 != 1);
    for (unsigned char c : s)
        check((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
              c == '_');
    std::string in = s;
    for (auto& c : in)
    {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
    }
    while (in.size() % 4)
        in += '=';
    std::string out(in.size() / 4 * 3, '\0');
    if (in.empty())
        return out;
    int length = EVP_DecodeBlock(ptr(out), ptr(in), static_cast<int>(in.size()));
    check(length >= 0);
    if (in.back() == '=')
        --length;
    if (in.size() > 1 && in[in.size() - 2] == '=')
        --length;
    out.resize(length);
    check(encode(out) == s);
    return out;
}
std::string digest(const std::string& s)
{
    unsigned char out[32];
    unsigned int length = 0;
    check(EVP_Digest(s.data(), s.size(), out, &length, EVP_sha256(), nullptr) == 1 && length == 32);
    const char* hex = "0123456789abcdef";
    std::string result;
    for (auto b : out)
    {
        result += hex[b >> 4];
        result += hex[b & 15];
    }
    return result;
}
std::string publicKey(const std::string& raw)
{
    auto key = privateKey(raw);
    std::string pub(32, '\0');
    size_t size = pub.size();
    check(EVP_PKEY_get_raw_public_key(key.get(), ptr(pub), &size) == 1 && size == 32);
    return pub;
}
Json seal(const std::string& rawKey, const std::string& recipient, const std::string& topic,
          const std::string& direction, const std::string& id, const Json& payload)
{
    const auto issued = now();
    check(recipient.size() == 32);
    auto key = privateKey(rawKey);
    Context ctx(OSSL_HPKE_CTX_new(OSSL_HPKE_MODE_AUTH, suite, OSSL_HPKE_ROLE_SENDER, nullptr, nullptr),
                OSSL_HPKE_CTX_free);
    check(ctx && OSSL_HPKE_CTX_set1_authpriv(ctx.get(), key.get()) == 1);
    std::string enc(32, '\0');
    size_t encSize = enc.size();
    check(OSSL_HPKE_encap(ctx.get(), ptr(enc), &encSize, ptr(recipient), recipient.size(), ptr(info),
                          info.size()) == 1);
    enc.resize(encSize);
    Json envelope{{"v", 2},
                  {"id", id},
                  {"sender", encode(publicKey(rawKey))},
                  {"recipient", encode(recipient)},
                  {"issued_at", issued},
                  {"enc", encode(enc)}};
    const auto associated = aad(direction, topic, envelope), plain = payload.dump();
    std::string cipher(OSSL_HPKE_get_ciphertext_size(suite, plain.size()), '\0');
    size_t size = cipher.size();
    check(OSSL_HPKE_seal(ctx.get(), ptr(cipher), &size, ptr(associated), associated.size(), ptr(plain),
                         plain.size()) == 1);
    cipher.resize(size);
    envelope["ciphertext"] = encode(cipher);
    check(envelope.dump().size() <= 49152);
    return envelope;
}
Message open(const std::string& rawKey, const std::string& topic, const std::string& direction,
             const std::string& raw)
{
    const auto time = now();
    check(raw.size() <= 49152);
    const auto envelope = Json::parse(raw);
    check(envelope.at("v") == 2 && envelope.at("issued_at").is_number_integer());
    const auto issued = envelope.at("issued_at").get<int64_t>();
    check(issued >= time - 120 && issued <= time + 120 &&
          envelope.at("recipient") == encode(publicKey(rawKey)));
    auto sender = decode(envelope.at("sender").get<std::string>()),
         enc = decode(envelope.at("enc").get<std::string>()),
         cipher = decode(envelope.at("ciphertext").get<std::string>());
    check(sender.size() == 32 && enc.size() == 32 && cipher.size() >= 16);
    auto key = privateKey(rawKey);
    Context ctx(OSSL_HPKE_CTX_new(OSSL_HPKE_MODE_AUTH, suite, OSSL_HPKE_ROLE_RECEIVER, nullptr, nullptr),
                OSSL_HPKE_CTX_free);
    check(ctx && OSSL_HPKE_CTX_set1_authpub(ctx.get(), ptr(sender), sender.size()) == 1);
    check(OSSL_HPKE_decap(ctx.get(), ptr(enc), enc.size(), key.get(), ptr(info), info.size()) == 1);
    const auto associated = aad(direction, topic, envelope);
    std::string plain(cipher.size(), '\0');
    size_t size = plain.size();
    check(OSSL_HPKE_open(ctx.get(), ptr(plain), &size, ptr(associated), associated.size(), ptr(cipher),
                         cipher.size()) == 1);
    plain.resize(size);
    return {sender, envelope.at("id").get<std::string>(), Json::parse(plain)};
}
} // namespace merchant::pos
