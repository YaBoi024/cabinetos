// The host's picture (capture.h).

#include "capture.h"

#include <spa/buffer/meta.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace {

// gamescope's own field for the size it should scale to (pipewire_gamescope.hpp).
constexpr uint32_t kRequestedSize = 0x70000;

struct Find {
    pw_thread_loop* loop = nullptr;
    pw_core* core = nullptr;
    int pid = 0;
    std::map<uint32_t, int> clientPid;    // client id, the process behind it
    std::map<uint32_t, uint32_t> nodeClient;
    int syncSeq = 0;
    bool done = false;
};

void onGlobal(void* data, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props) {
    auto* f = static_cast<Find*>(data);
    if (!props) return;
    if (std::strcmp(type, PW_TYPE_INTERFACE_Client) == 0) {
        // pipewire.sec.pid comes from the socket, so a client cannot state another's.
        if (const char* pid = spa_dict_lookup(props, PW_KEY_SEC_PID)) f->clientPid[id] = std::atoi(pid);
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        const char* client = spa_dict_lookup(props, PW_KEY_CLIENT_ID);
        if (name && client && std::strcmp(name, "gamescope") == 0)
            f->nodeClient[id] = static_cast<uint32_t>(std::atoi(client));
    }
}

const pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = onGlobal,
};

void onCoreDone(void* data, uint32_t, int seq) {
    auto* f = static_cast<Find*>(data);
    if (seq == f->syncSeq) {
        f->done = true;
        pw_thread_loop_signal(f->loop, false);
    }
}

const pw_core_events kCoreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .done = onCoreDone,
};

const spa_pod* buildFormat(spa_pod_builder* b, int askW, int askH) {
    spa_rectangle def = {static_cast<uint32_t>(askW), static_cast<uint32_t>(askH)};
    spa_rectangle least = {1, 1};
    spa_rectangle most = {8192, 8192};
    spa_fraction rate = {0, 1};
    spa_fraction rateLeast = {0, 1};
    spa_fraction rateMost = {1000, 1};
    spa_rectangle asked = {static_cast<uint32_t>(askW), static_cast<uint32_t>(askH)};
    spa_pod_frame frame;
    spa_pod_builder_push_object(b, &frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
                        SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_NV12),
                        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def, &least, &most),
                        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&rate, &rateLeast, &rateMost), 0);
    spa_pod_builder_add(b, kRequestedSize, SPA_POD_Rectangle(&asked), 0);
    return static_cast<const spa_pod*>(spa_pod_builder_pop(b, &frame));
}

const pw_stream_events* streamEvents(void (*param)(void*, uint32_t, const spa_pod*),
                                     void (*state)(void*, pw_stream_state, pw_stream_state, const char*),
                                     void (*process)(void*)) {
    static pw_stream_events events = {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.param_changed = param;
    events.state_changed = state;
    events.process = process;
    return &events;
}

}  // namespace

Capture::~Capture() { stop(); }

uint32_t Capture::findNode(int gamescopePid) {
    Find f;
    f.pid = gamescopePid;
    f.loop = pw_thread_loop_new("cabinetos-linkup-find", nullptr);
    if (!f.loop) return 0;
    pw_context* context = pw_context_new(pw_thread_loop_get_loop(f.loop), nullptr, 0);
    pw_thread_loop_lock(f.loop);
    pw_thread_loop_start(f.loop);
    uint32_t found = 0;
    f.core = context ? pw_context_connect(context, nullptr, 0) : nullptr;
    if (f.core) {
        spa_hook coreHook{};
        spa_hook registryHook{};
        pw_core_add_listener(f.core, &coreHook, &kCoreEvents, &f);
        pw_registry* registry = pw_core_get_registry(f.core, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener(registry, &registryHook, &kRegistryEvents, &f);
        f.syncSeq = pw_core_sync(f.core, PW_ID_CORE, 0);
        while (!f.done) {
            timespec until{};
            pw_thread_loop_get_time(f.loop, &until, 3 * SPA_NSEC_PER_SEC);
            if (pw_thread_loop_timed_wait_full(f.loop, &until) != 0) break;
        }
        for (const auto& [node, client] : f.nodeClient) {
            auto it = f.clientPid.find(client);
            if (it != f.clientPid.end() && it->second == gamescopePid) {
                found = node;
                break;
            }
        }
        spa_hook_remove(&registryHook);
        spa_hook_remove(&coreHook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
        pw_core_disconnect(f.core);
    }
    pw_thread_loop_unlock(f.loop);
    pw_thread_loop_stop(f.loop);
    if (context) pw_context_destroy(context);
    pw_thread_loop_destroy(f.loop);
    return found;
}

void Capture::onParam(void* self, uint32_t id, const spa_pod* param) {
    auto* me = static_cast<Capture*>(self);
    if (!param || id != SPA_PARAM_Format) return;
    spa_video_info_raw info{};
    if (spa_format_video_raw_parse(param, &info) < 0) return;
    me->w_ = static_cast<int>(info.size.width);
    me->h_ = static_cast<int>(info.size.height);
    std::fprintf(stderr, "[linkup] capture: %dx%d\n", me->w_, me->h_);
    uint8_t buf[512];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    const spa_pod* params[2];
    // At most 8 buffers: gamescope issue #775, more hung the AMD video engine.
    params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
        SPA_POD_CHOICE_RANGE_Int(4, 2, 8), SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(1 << SPA_DATA_MemFd)));
    params[1] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
        SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_header))));
    pw_stream_update_params(me->stream_, params, 2);
}

void Capture::onState(void*, pw_stream_state, pw_stream_state now, const char* error) {
    if (now == PW_STREAM_STATE_ERROR) std::fprintf(stderr, "[linkup] capture failed: %s\n", error ? error : "");
}

void Capture::onProcess(void* self) {
    auto* me = static_cast<Capture*>(self);
    pw_buffer* latest = nullptr;
    while (pw_buffer* next = pw_stream_dequeue_buffer(me->stream_)) {
        if (latest) pw_stream_queue_buffer(me->stream_, latest);
        latest = next;
    }
    if (!latest) return;
    spa_buffer* sb = latest->buffer;
    const spa_data& d0 = sb->datas[0];
    // Larger than what was asked for: gamescope has not taken up the size yet.
    const bool right = me->w_ <= me->askW_ && me->h_ <= me->askH_;
    if (me->active_.load() && right && d0.data && d0.chunk->size > 0 && me->onFrame_) {
        const auto* base = static_cast<const uint8_t*>(d0.data) + d0.chunk->offset;
        const int yStride = d0.chunk->stride;
        const uint8_t* uv = base + static_cast<size_t>(yStride) * static_cast<size_t>(me->h_);
        int uvStride = yStride;
        if (sb->n_datas > 1 && sb->datas[1].data) {
            uv = static_cast<const uint8_t*>(sb->datas[1].data) + sb->datas[1].chunk->offset;
            uvStride = sb->datas[1].chunk->stride;
        }
        me->onFrame_(base, yStride, uv, uvStride, me->w_, me->h_);
    }
    pw_stream_queue_buffer(me->stream_, latest);
}

bool Capture::start(uint32_t node, int width, int height, OnFrame onFrame) {
    if (stream_ || node == 0 || node == PW_ID_ANY) return false;
    onFrame_ = std::move(onFrame);
    askW_ = width;
    askH_ = height;
    active_ = true;
    loop_ = pw_thread_loop_new("cabinetos-linkup-capture", nullptr);
    if (!loop_) return false;
    pw_thread_loop_lock(loop_);
    stream_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), "cabinetos-linkup",
                                   pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                     PW_KEY_MEDIA_ROLE, "Screen", nullptr),
                                   streamEvents(onParam, onState, onProcess), this);
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    const spa_pod* format = buildFormat(&b, width, height);
    // No AUTOCONNECT to anything else: the node asked for, and if it has gone, an error.
    const int r = pw_stream_connect(stream_, PW_DIRECTION_INPUT, node,
                                    static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                                 PW_STREAM_FLAG_DONT_RECONNECT),
                                    &format, 1);
    pw_thread_loop_unlock(loop_);
    if (r < 0) {
        std::fprintf(stderr, "[linkup] capture would not connect: %d\n", r);
        return false;
    }
    pw_thread_loop_start(loop_);
    return true;
}

void Capture::setActive(bool on) {
    active_ = on;
    if (!stream_) return;
    pw_thread_loop_lock(loop_);
    pw_stream_set_active(stream_, on);
    pw_thread_loop_unlock(loop_);
}

void Capture::setSize(int width, int height) {
    if (!stream_) return;
    pw_thread_loop_lock(loop_);
    askW_ = width;
    askH_ = height;
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    const spa_pod* format = buildFormat(&b, width, height);
    pw_stream_update_params(stream_, &format, 1);
    pw_thread_loop_unlock(loop_);
}

void Capture::stop() {
    if (!stream_) return;
    // Off first, a beat for gamescope to finish any frame, then away.
    active_ = false;
    pw_thread_loop_lock(loop_);
    pw_stream_set_active(stream_, false);
    pw_thread_loop_unlock(loop_);
    usleep(150000);
    pw_thread_loop_lock(loop_);
    pw_stream_disconnect(stream_);
    pw_stream_destroy(stream_);
    stream_ = nullptr;
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_stop(loop_);
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
}
