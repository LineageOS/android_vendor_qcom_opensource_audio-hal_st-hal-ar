/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "STHAL: SoundTriggerHw"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <soundtriggerhw/SoundTriggerHw.h>
#include <soundtriggerhw/SoundTriggerCommon.h>
#include <log/log.h>

using aidl::android::hardware::soundtrigger3::SoundTriggerHw;

static inline binder_status_t registerServiceImpl(bool stubMode)
{
    std::shared_ptr<SoundTriggerHw> soundTriggerFactory =
                ::ndk::SharedRefBase::make<SoundTriggerHw>(stubMode /* stub */);

    binder_status_t status = soundTriggerFactory->isInitDone();
    if (!status) {
        STHAL_ERR(LOG_TAG, "SoundTriggerHw initialization failed.");
        return STATUS_INVALID_OPERATION;
    }

    const std::string stInterfaceName =
        std::string() + SoundTriggerHw::descriptor + "/default";
    status = AServiceManager_addService(
        soundTriggerFactory->asBinder().get(), stInterfaceName.c_str());

    if (status != STATUS_OK) {
        STHAL_WARN(LOG_TAG, "Could not register %s, status=%d",
            stInterfaceName.c_str(), status);
    }
    return status;
}

/**
 * Creates the standard implementation of the SoundTrigger factory
 * that communicates with the actual hardware.
 */
extern "C" __attribute__((visibility("default"))) binder_status_t
createISoundTriggerFactory()
{
    return registerServiceImpl(false /* stubMode */);
}

/**
 * Creates a stub implementation of the SoundTrigger factory.
 * This is used for testing or when the actual hardware implementation
 * does not exist.
 */
extern "C" __attribute__((visibility("default"))) binder_status_t
createStubISoundTriggerFactory()
{
    return registerServiceImpl(true /* stubMode */);
}

