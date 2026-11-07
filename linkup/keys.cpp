// What this console keeps for LinkUp (keys.h).

#include "keys.h"

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#include <openssl/crypto.h>

#include "fileio.h"
#include "hexutil.h"
#include "jsonutil.h"

Keys::Keys(std::string folder) : folder_(std::move(folder)) {}

bool Keys::load() {
    std::lock_guard<std::mutex> lk(mu_);
    mkdir(folder_.c_str(), 0700);
    chmod(folder_.c_str(), 0700);
    const std::string keyPath = folder_ + "/key";
    const std::vector<uint8_t> held = cab::readBytes(keyPath);
    noise::Key priv{};
    if (held.size() == 64 && hexutil::decode(std::string(held.begin(), held.end()), priv)) {
        if (!noise::keyPairFrom(priv, mine_)) return false;
    } else {
        if (!noise::newKeyPair(mine_)) return false;
        std::string hex = hexutil::encode(mine_.priv.data(), mine_.priv.size());
        const bool wrote = cab::writeBytes(keyPath, hex, 0600);
        OPENSSL_cleanse(hex.data(), hex.size());
        if (!wrote) {
            std::fprintf(stderr, "[linkup] could not keep this console's key\n");
            return false;
        }
    }
    OPENSSL_cleanse(priv.data(), priv.size());

    const std::vector<uint8_t> saved = cab::readBytes(folder_ + "/server.json");
    if (!saved.empty()) {
        json_object* o = jsonutil::parse(std::string(saved.begin(), saved.end()));
        Server s;
        s.address = jsonutil::str(o, "address");
        s.name = jsonutil::str(o, "name");
        if (!s.address.empty() && hexutil::decode(jsonutil::str(o, "key"), s.key)) server_ = s;
        if (o) json_object_put(o);
    }
    const std::vector<uint8_t> t = cab::readBytes(folder_ + "/last-t");
    if (!t.empty()) lastT_ = std::strtoll(std::string(t.begin(), t.end()).c_str(), nullptr, 10);
    return true;
}

std::optional<Keys::Server> Keys::server() const {
    std::lock_guard<std::mutex> lk(mu_);
    return server_;
}

bool Keys::saveServer() {
    json_object* o = json_object_new_object();
    jsonutil::set(o, "address", server_->address);
    jsonutil::set(o, "name", server_->name);
    jsonutil::set(o, "key", hexutil::encode(server_->key.data(), server_->key.size()));
    const std::string text = jsonutil::text(o);
    json_object_put(o);
    return cab::writeBytes(folder_ + "/server.json", text, 0600);
}

bool Keys::remember(const Server& s) {
    std::lock_guard<std::mutex> lk(mu_);
    server_ = s;
    return saveServer();
}

bool Keys::setAddress(const std::string& address) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!server_) return false;
    server_->address = address;
    return saveServer();
}

bool Keys::setName(const std::string& name) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!server_ || server_->name == name) return true;
    server_->name = name;
    return saveServer();
}

bool Keys::forget() {
    std::lock_guard<std::mutex> lk(mu_);
    server_.reset();
    return cab::removeFile(folder_ + "/server.json");
}

int64_t Keys::nextT() {
    std::lock_guard<std::mutex> lk(mu_);
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    lastT_ = now > lastT_ ? now : lastT_ + 1;
    cab::writeBytes(folder_ + "/last-t", std::to_string(lastT_), 0600);
    return lastT_;
}
