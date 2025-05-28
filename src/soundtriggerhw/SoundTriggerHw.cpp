/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "STHAL: SoundTriggerHw"

#include <android/binder_status.h>
#include <cutils/properties.h>
#include <dlfcn.h>
#include <log/log.h>
#include <soundtriggerhw/SoundTriggerHw.h>
#include <soundtriggerhw/SoundTriggerCommon.h>
#include <utils/CoreUtils.h>
#include <utils/PalToAidlConverter.h>
#include "PalApi.h"

using android::OK;

// NOTE: Follow same enum definition with audio hal(hal/service/Services.cpp)
enum class StubMode {
    STUB_DISABLED = 0,
    STUB_ENABLED = 1 << 0,
    AUTO_RECOVERY_ENABLED = 1 << 2,
};

//Returns retVal incase of invalid session
#define CHECK_VALID_SESSION(session, handle, retVal)                 \
    ({                                                               \
        if (session == nullptr) {                                    \
            STHAL_ERR(LOG_TAG, "invalid handle %d", handle);         \
            return CoreUtils::halErrorToAidl(retVal);                \
        }                                                            \
    })

// Return success if stub hal is enabled
#define RETURN_IF_STUB_HAL_ENABLED()                                      \
    ({                                                                    \
        if (mStubHal) {                                                   \
            STHAL_INFO(LOG_TAG, "Exit with success as Stub HAL enabled"); \
            return CoreUtils::halErrorToAidl(0);                          \
        }                                                                 \
    })

namespace aidl::android::hardware::soundtrigger3 {

SoundTriggerHw::SoundTriggerHw()
{
    char prop_value[PROPERTY_VALUE_MAX];

    STHAL_INFO(LOG_TAG, "Enter");
    mSoundTriggerInitDone = true;
    property_get("vendor.audio.hal.stubmode", prop_value, "0");
    mStubHal = (atoi(prop_value) == (int)StubMode::STUB_ENABLED);
}

SoundTriggerHw::~SoundTriggerHw()
{
    STHAL_INFO(LOG_TAG, "Enter");
}

ScopedAStatus SoundTriggerHw::registerGlobalCallback(
    const std::shared_ptr<ISoundTriggerHwGlobalCallback> &callback)
{
    int status = 0;
    pal_param_resources_available_t param_resource_avail;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_VERBOSE(LOG_TAG, "Enter");

    mGlobalCallback = callback;
    param_resource_avail.callback = (void*)&onResourcesAvailable;
    param_resource_avail.cookie = (uint64_t)this;

    status = pal_set_param(PAL_PARAM_ID_RESOURCES_AVAILABLE,
                          (void*)&param_resource_avail,
                          sizeof(pal_param_resources_available_t));
    if (status) {
        STHAL_ERR(LOG_TAG, "failed to set paramID for resources available, status %d",
            status);
    }

    STHAL_INFO(LOG_TAG, "Exit, status %d", status);
    return CoreUtils::halErrorToAidl(status);
}

std::shared_ptr<SoundTriggerSession> SoundTriggerHw::getSession(int32_t handle)
{
    std::shared_ptr<SoundTriggerSession> client = nullptr;

    std::lock_guard<std::mutex> lock(mMutex);
    if (mSessions.find(handle) != mSessions.end()) {
        client = mSessions[handle];
    } else {
        STHAL_ERR(LOG_TAG, "client not found for handle %d", handle);
    }
    return client;
}

void SoundTriggerHw::addSession(std::shared_ptr<SoundTriggerSession> &session)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mSessions[session->getSessionHandle()] = session;

    STHAL_INFO(LOG_TAG, "handle %d, sessions %d", session->getSessionHandle(), mSessions.size());
}

void SoundTriggerHw::removeSession(int32_t handle)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mSessions.erase(handle);
}

ScopedAStatus SoundTriggerHw::getProperties(Properties *aidlProperties)
{
    int status = 0;
    struct pal_st_properties *palProperties = nullptr;
    size_t size = 0;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_VERBOSE(LOG_TAG, "Enter");

    status = pal_get_param(PAL_PARAM_ID_GET_SOUND_TRIGGER_PROPERTIES,
                         (void **)&palProperties, &size, nullptr);

    if (status || !palProperties || size < sizeof(struct pal_st_properties)) {
        STHAL_ERR(LOG_TAG, "query properties from pal failed, status %d", status);
        return CoreUtils::halErrorToAidl(status);
    }

    PalToAidlConverter::convertProperties(palProperties, *aidlProperties);

    auto st_session = std::make_shared<SoundTriggerSession>(0, nullptr);
    aidlProperties->supportedModelArch = st_session->getModuleVersion();

    STHAL_INFO(LOG_TAG, "Exit properties %s, status %d",
                            aidlProperties->toString().c_str(), status);
    return CoreUtils::halErrorToAidl(status);
}

ScopedAStatus SoundTriggerHw::loadSoundModel(
    const SoundModel &model,
    const std::shared_ptr<ISoundTriggerHwCallback> &callback,
    int32_t *handle)
{
    int status = 0;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_INFO(LOG_TAG, "Enter");

    *handle = nextUniqueModelId();
    auto st_session = std::make_shared<SoundTriggerSession>(*handle, callback);
    status = st_session->loadSoundModel(model);

    if (status) {
        STHAL_ERR(LOG_TAG, "Failed to load sound model with handle %d, status %d",
                                                    *handle, status);
        handle = nullptr;
        return CoreUtils::halErrorToAidl(status);
    }

    addSession(st_session);

    STHAL_INFO(LOG_TAG, "Exit handle %d, status %d", *handle, status);
    return ndk::ScopedAStatus::ok();
}

ScopedAStatus SoundTriggerHw::loadPhraseSoundModel(
    const PhraseSoundModel &model,
    const std::shared_ptr<ISoundTriggerHwCallback> &callback,
    int32_t *handle)
{
    int status = 0;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_INFO(LOG_TAG, "Enter");

    *handle = nextUniqueModelId();
    auto st_session = std::make_shared<SoundTriggerSession>(*handle, callback);
    status = st_session->loadPhraseSoundModel(model);

    if (status) {
        STHAL_ERR(LOG_TAG, "Failed to load phrase sound model with handle %d, status %d",
                                                    *handle, status);
        handle = nullptr;
        return CoreUtils::halErrorToAidl(status);
    }

    addSession(st_session);

    STHAL_INFO(LOG_TAG, "Exit handle %d, status %d", *handle, status);
    return ndk::ScopedAStatus::ok();
}

ScopedAStatus SoundTriggerHw::unloadSoundModel(int32_t handle)
{
    int status = 0;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_INFO(LOG_TAG, "Enter handle %d", handle);

    auto st_session = getSession(handle);
    CHECK_VALID_SESSION(st_session, handle, STATUS_INVALID_OPERATION);

    status = st_session->unloadSoundModel();
    if (status != 0) {
        STHAL_ERR(LOG_TAG, "Failed to unload sound model with handle %d, status %d",
                                                        handle, status);
        return CoreUtils::halErrorToAidl(status);
    }

    removeSession(handle);

    STHAL_INFO(LOG_TAG, "Exit handle %d, status %d", handle, status);
    return CoreUtils::halErrorToAidl(status);
}

ScopedAStatus SoundTriggerHw::startRecognition(
    int32_t modelHandle,
    int32_t deviceHandle,
    int32_t ioHandle,
    const RecognitionConfig &config)
{
    int status = 0;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_INFO(LOG_TAG, "Enter handle %d", modelHandle);

    auto st_session = getSession(modelHandle);
    CHECK_VALID_SESSION(st_session, modelHandle, STATUS_INVALID_OPERATION);

    status = st_session->startRecognition(deviceHandle, ioHandle, config);
    if (status != 0) {
        STHAL_ERR(LOG_TAG, "Failed to start recognition model with handle %d, status %d",
                                                       ioHandle, status);
        return CoreUtils::halErrorToAidl(status);
    }

    STHAL_INFO(LOG_TAG, "Exit handle %d, status %d", modelHandle, status);
    return CoreUtils::halErrorToAidl(status);
}

ScopedAStatus SoundTriggerHw::stopRecognition(int32_t handle)
{
    int status = 0;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_INFO(LOG_TAG, "Enter handle %d", handle);

    auto st_session = getSession(handle);
    CHECK_VALID_SESSION(st_session, handle, STATUS_INVALID_OPERATION);

    status = st_session->stopRecognition();
    if (status != 0) {
        STHAL_ERR(LOG_TAG, "Failed to stop recognition model with handle %d, status %d",
                                                      handle, status);
        return CoreUtils::halErrorToAidl(status);
    }

    STHAL_INFO(LOG_TAG, "Exit handle %d, status %d", handle, status);
    return CoreUtils::halErrorToAidl(status);
}

ScopedAStatus SoundTriggerHw::forceRecognitionEvent(int32_t handle)
{
    int status = 0;

    RETURN_IF_STUB_HAL_ENABLED();

    STHAL_INFO(LOG_TAG, "Enter handle %d", handle);

    auto st_session = getSession(handle);
    CHECK_VALID_SESSION(st_session, handle, STATUS_INVALID_OPERATION);

    status = st_session->forceRecognitionEvent();
    if (status != 0) {
        STHAL_ERR(LOG_TAG, "Failed to force recognition event with handle %d, status %d",
                                                      handle, status);
        return CoreUtils::halErrorToAidl(status);
    }

    STHAL_INFO(LOG_TAG, "Exit handle %d, status %d", handle, status);
    return CoreUtils::halErrorToAidl(status);
}

ScopedAStatus SoundTriggerHw::queryParameter(
    int32_t handle,
    ModelParameter modelParams,
    std::optional<ModelParameterRange> *aidlRange)
{
    int status = -ENOSYS;

    STHAL_INFO(LOG_TAG, "unsupported API");
    return CoreUtils::halErrorToAidl(status);
}

ScopedAStatus SoundTriggerHw::getParameter(
    int32_t handle,
    ModelParameter modelParams,
    int32_t *aidlParameters)
{
    int status = -ENOSYS;

    STHAL_INFO(LOG_TAG, "unsupported API");
    return CoreUtils::halErrorToAidl(status);
}

ScopedAStatus SoundTriggerHw::setParameter(
    int32_t handle,
    ModelParameter modelParams,
    int32_t value)
{
    int status = -ENOSYS;

    STHAL_INFO(LOG_TAG, "unsupported API");
    return CoreUtils::halErrorToAidl(status);
}

void SoundTriggerHw::onResourcesAvailable(uint64_t cookie)
{
    SoundTriggerHw *hw = (SoundTriggerHw *)cookie;

    if (hw)
        hw->mGlobalCallback->onResourcesAvailable();

    STHAL_INFO(LOG_TAG, "Exit");
}

} // namespace aidl::android::hardware::soundtrigger3
